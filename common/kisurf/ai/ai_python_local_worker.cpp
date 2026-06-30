/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf/ai/ai_python_local_worker.h>
#include <kisurf/ai/ai_python_worker_protocol.h>

#include <paths.h>

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stopwatch.h>
#include <wx/utils.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef __WXMSW__
#include <windows.h>
#include <cwchar>
#endif

struct AI_PYTHON_LOCAL_PROCESS
{
#ifdef __WXMSW__
    HANDLE m_Process = nullptr;
    HANDLE m_StdinWrite = nullptr;
    HANDLE m_StdoutRead = nullptr;
    HANDLE m_StderrRead = nullptr;
    HANDLE m_ControlWrite = nullptr;
    HANDLE m_EventRead = nullptr;
    DWORD  m_ProcessId = 0;
    std::thread m_EventThread;
    std::atomic_bool m_StopEventReader{ false };
#endif
};

namespace
{
AI_PYTHON_CELL_RESULT workerError( const wxString& aCode, const wxString& aMessage,
                                   const wxString& aStdout = wxEmptyString,
                                   const wxString& aStderr = wxEmptyString )
{
    AI_PYTHON_CELL_RESULT result;
    result.m_Ok = false;
    result.m_ErrorCode = aCode;
    result.m_Message = aMessage;
    result.m_Stdout = aStdout;
    result.m_Stderr = aStderr;
    return result;
}


wxString fromUtf8String( const std::string& aText )
{
    if( aText.empty() )
        return wxEmptyString;

    return wxString::FromUTF8( aText.c_str() );
}


std::optional<size_t> parseFramePayloadLength( const std::string& aHeader )
{
    static constexpr const char* prefix = "KISURF_AI_FRAME_V1 ";
    const std::string           prefixText( prefix );

    if( aHeader.rfind( prefixText, 0 ) != 0 )
        return std::nullopt;

    try
    {
        return static_cast<size_t>( std::stoull( aHeader.substr( prefixText.size() ) ) );
    }
    catch( const std::exception& )
    {
        return std::nullopt;
    }
}


#ifdef __WXMSW__
void closeHandle( HANDLE& aHandle )
{
    if( aHandle )
    {
        CloseHandle( aHandle );
        aHandle = nullptr;
    }
}


wxString windowsErrorText( DWORD aErrorCode )
{
    wchar_t* buffer = nullptr;

    FormatMessageW( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                            | FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr, aErrorCode, 0, reinterpret_cast<LPWSTR>( &buffer ),
                    0, nullptr );

    wxString message = buffer ? wxString( buffer )
                              : wxString::Format( wxS( "Windows error %lu" ),
                                                  static_cast<unsigned long>( aErrorCode ) );

    if( buffer )
        LocalFree( buffer );

    message.Trim( true );
    message.Trim( false );
    return message;
}


bool processIsRunning( const AI_PYTHON_LOCAL_PROCESS* aProcess )
{
    if( !aProcess || !aProcess->m_Process )
        return false;

    DWORD exitCode = 0;

    if( !GetExitCodeProcess( aProcess->m_Process, &exitCode ) )
        return false;

    return exitCode == STILL_ACTIVE;
}


std::wstring quoteCommandArgument( const wxString& aText )
{
    std::wstring text = aText.ToStdWstring();
    std::wstring quoted = L"\"";

    for( wchar_t ch : text )
    {
        if( ch == L'"' )
            quoted += L'\\';

        quoted += ch;
    }

    quoted += L"\"";
    return quoted;
}


std::vector<wchar_t> buildEnvironmentBlock( const wxString& aSdkRootPath )
{
    std::vector<std::wstring> entries;
    wchar_t* env = GetEnvironmentStringsW();

    if( env )
    {
        for( wchar_t* cursor = env; *cursor; )
        {
            std::wstring entry = cursor;

            if( _wcsnicmp( entry.c_str(), L"PYTHONPATH=", 11 ) != 0
                && _wcsnicmp( entry.c_str(), L"PYTHONHOME=", 11 ) != 0 )
            {
                entries.push_back( entry );
            }

            cursor += entry.size() + 1;
        }

        FreeEnvironmentStringsW( env );
    }

    entries.push_back( L"PYTHONPATH=" + aSdkRootPath.ToStdWstring() );

    std::vector<wchar_t> block;

    for( const std::wstring& entry : entries )
    {
        block.insert( block.end(), entry.begin(), entry.end() );
        block.push_back( L'\0' );
    }

    block.push_back( L'\0' );
    return block;
}


wxString drainHandle( HANDLE aHandle )
{
    if( !aHandle )
        return wxEmptyString;

    std::string bytes;

    for( ;; )
    {
        DWORD available = 0;

        if( !PeekNamedPipe( aHandle, nullptr, 0, nullptr, &available, nullptr )
            || available == 0 )
        {
            break;
        }

        char  buffer[1024];
        DWORD readCount = 0;
        const DWORD toRead = std::min<DWORD>( available, sizeof( buffer ) );

        if( !ReadFile( aHandle, buffer, toRead, &readCount, nullptr ) || readCount == 0 )
            break;

        bytes.append( buffer, readCount );
    }

    return fromUtf8String( bytes );
}


bool readFrameFromHandle( HANDLE aHandle, std::string* aFrame, DWORD* aError )
{
    if( aFrame )
        aFrame->clear();

    if( aError )
        *aError = 0;

    std::string header;

    for( ;; )
    {
        char  ch = 0;
        DWORD readCount = 0;

        const BOOL ok = ReadFile( aHandle, &ch, 1, &readCount, nullptr );

        if( !ok || readCount == 0 )
        {
            if( aError )
                *aError = GetLastError();

            return false;
        }

        if( ch == '\n' )
            break;

        if( ch != '\r' )
            header.push_back( ch );
    }

    std::optional<size_t> payloadLength = parseFramePayloadLength( header );

    if( !payloadLength )
    {
        if( aError )
            *aError = ERROR_INVALID_DATA;

        return false;
    }

    std::string frame = header + "\n";
    size_t      remaining = *payloadLength;

    while( remaining > 0 )
    {
        char  buffer[1024];
        DWORD readCount = 0;
        const DWORD toRead =
                static_cast<DWORD>( std::min<size_t>( remaining, sizeof( buffer ) ) );

        const BOOL ok = ReadFile( aHandle, buffer, toRead, &readCount, nullptr );

        if( !ok || readCount == 0 )
        {
            if( aError )
                *aError = GetLastError();

            return false;
        }

        frame.append( buffer, readCount );
        remaining -= readCount;
    }

    if( aFrame )
        *aFrame = std::move( frame );

    return true;
}


void closeProcessHandles( AI_PYTHON_LOCAL_PROCESS* aProcess )
{
    if( !aProcess )
        return;

    closeHandle( aProcess->m_StdinWrite );
    closeHandle( aProcess->m_StdoutRead );
    closeHandle( aProcess->m_StderrRead );
    closeHandle( aProcess->m_ControlWrite );
    closeHandle( aProcess->m_EventRead );
    closeHandle( aProcess->m_Process );
    aProcess->m_ProcessId = 0;
}
#endif
} // namespace


AI_PYTHON_LOCAL_WORKER::AI_PYTHON_LOCAL_WORKER( wxString aInterpreterPath,
                                                wxString aSdkRootPath,
                                                long aResponseTimeoutMs ) :
        m_InterpreterPath( std::move( aInterpreterPath ) ),
        m_SdkRootPath( std::move( aSdkRootPath ) ),
        m_ResponseTimeoutMs( std::max<long>( 1, aResponseTimeoutMs ) )
{
}


AI_PYTHON_LOCAL_WORKER::~AI_PYTHON_LOCAL_WORKER()
{
    stopProcess( false );
}


wxString AI_PYTHON_LOCAL_WORKER::DefaultInterpreterPath()
{
    return wxS( "python" );
}


wxString AI_PYTHON_LOCAL_WORKER::DefaultSdkRootPath()
{
#ifdef KISURF_AI_PYTHON_SDK_SOURCE_DIR
    const wxString sourceRoot =
            wxString::FromUTF8Unchecked( KISURF_AI_PYTHON_SDK_SOURCE_DIR );

    if( wxDirExists( sourceRoot ) )
        return sourceRoot;
#endif

    return InstalledSdkRootPath();
}


wxString AI_PYTHON_LOCAL_WORKER::InstalledSdkRootPath()
{
    wxFileName path( PATHS::GetStockScriptingPath(), wxEmptyString );
    path.AppendDir( wxS( "kisurf_ai_session_sdk" ) );
    return path.GetPath();
}


std::unique_ptr<AI_PYTHON_WORKER> AI_PYTHON_LOCAL_WORKER::CreateDefault()
{
    return std::make_unique<AI_PYTHON_LOCAL_WORKER>(
            DefaultInterpreterPath(), DefaultSdkRootPath() );
}


bool AI_PYTHON_LOCAL_WORKER::IsConnected() const
{
    return !m_InterpreterPath.IsEmpty() && !m_SdkRootPath.IsEmpty()
           && wxDirExists( m_SdkRootPath );
}


void AI_PYTHON_LOCAL_WORKER::SetEventSink( AI_PYTHON_EVENT_SINK* aSink )
{
    m_EventSink = aSink;
}


AI_PYTHON_CELL_RESULT AI_PYTHON_LOCAL_WORKER::RunCell(
        const AI_EXECUTION_SESSION& aSession, const AI_PYTHON_CELL_REQUEST& aRequest )
{
    wxUnusedVar( aSession );
    m_LastSessionId.store( aRequest.m_SessionId );
    m_CancelledSessionId.store( 0 );
    m_CancelRequested.store( false );
    m_CellRunning.store( true );

    if( !IsConnected() )
    {
        m_CellRunning.store( false );
        return workerError( wxS( "python_worker_not_connected" ),
                            wxS( "Python interpreter or KiSurf SDK path is not configured." ) );
    }

    wxString error;

    if( !ensureProcess( &error ) )
    {
        m_CellRunning.store( false );
        return workerError( wxS( "python_worker_launch_failed" ), error );
    }

    const std::string requestPayload =
            AI_PYTHON_WORKER_PROTOCOL::EncodeRunCellRequest( aRequest );

    if( !writeRequestFrame( requestPayload, &error ) )
    {
        m_CellRunning.store( false );
        stopProcess( true );
        return workerError( wxS( "python_worker_request_failed" ), error );
    }

    std::string responsePayload;

    if( !readResponseFrame( &responsePayload, &error ) )
    {
        wxString stdOut = fromUtf8String( responsePayload );
        wxString stdErr;
        const bool timedOut = error.Contains( wxS( "Timed out" ) );

#ifdef __WXMSW__
        if( m_Process )
        {
            if( stdOut.IsEmpty() )
                stdOut = drainHandle( m_Process->m_StdoutRead );

            stdErr = drainHandle( m_Process->m_StderrRead );
        }
#endif

        const bool wasCancelled = m_CancelRequested.load()
                                  && m_CancelledSessionId.load()
                                             == aRequest.m_SessionId;

        m_CellRunning.store( false );
        stopProcess( true );

        if( wasCancelled )
        {
            m_CancelRequested.store( false );
            m_CancelledSessionId.store( 0 );

            return workerError( wxS( "python_cancelled" ),
                                wxS( "session cancelled" ), stdOut, stdErr );
        }

        return workerError( timedOut ? wxS( "python_worker_timeout" )
                                     : wxS( "python_worker_process_failed" ),
                            error, stdOut, stdErr );
    }

    wxString decodeError;
    AI_PYTHON_CELL_RESULT result =
            AI_PYTHON_WORKER_PROTOCOL::DecodeCellResult( responsePayload, &decodeError );

    if( !decodeError.IsEmpty() )
        result.m_Message = decodeError;

    m_CellRunning.store( false );
    m_CancelRequested.store( false );
    m_CancelledSessionId.store( 0 );
    return result;
}


void AI_PYTHON_LOCAL_WORKER::Cancel()
{
    const uint64_t sessionId = m_LastSessionId.load();

    if( sessionId != 0 )
    {
        m_CancelledSessionId.store( sessionId );
        m_CancelRequested.store( true );

        writeControlFrameBestEffort(
                AI_PYTHON_WORKER_PROTOCOL::EncodeCancelSessionRequest(
                        sessionId, wxS( "session cancelled" ) ) );

#ifdef __WXMSW__
        wxMilliSleep( 500 );

        if( m_CellRunning.load() && m_Process && processIsRunning( m_Process.get() )
            && m_CancelledSessionId.load() == sessionId )
        {
            TerminateProcess( m_Process->m_Process, 1 );
        }
#endif
    }
}


void AI_PYTHON_LOCAL_WORKER::HardKill()
{
    stopProcess( true );
}


bool AI_PYTHON_LOCAL_WORKER::ensureProcess( wxString* aError )
{
#ifdef __WXMSW__
    if( m_Process && m_Pid != 0 && processIsRunning( m_Process.get() ) )
        return true;

    if( m_Process )
        stopProcess( true );

    SECURITY_ATTRIBUTES security;
    security.nLength = sizeof( SECURITY_ATTRIBUTES );
    security.bInheritHandle = TRUE;
    security.lpSecurityDescriptor = nullptr;

    HANDLE stdinRead = nullptr;
    HANDLE stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stderrRead = nullptr;
    HANDLE stderrWrite = nullptr;
    HANDLE controlRead = nullptr;
    HANDLE controlWrite = nullptr;
    HANDLE eventRead = nullptr;
    HANDLE eventWrite = nullptr;

    if( !CreatePipe( &stdinRead, &stdinWrite, &security, 0 )
        || !CreatePipe( &stdoutRead, &stdoutWrite, &security, 0 )
        || !CreatePipe( &stderrRead, &stderrWrite, &security, 0 )
        || !CreatePipe( &controlRead, &controlWrite, &security, 0 )
        || !CreatePipe( &eventRead, &eventWrite, &security, 0 ) )
    {
        const DWORD errorCode = GetLastError();
        closeHandle( stdinRead );
        closeHandle( stdinWrite );
        closeHandle( stdoutRead );
        closeHandle( stdoutWrite );
        closeHandle( stderrRead );
        closeHandle( stderrWrite );
        closeHandle( controlRead );
        closeHandle( controlWrite );
        closeHandle( eventRead );
        closeHandle( eventWrite );

        if( aError )
            *aError = wxS( "Could not create Python worker pipes: " )
                      + windowsErrorText( errorCode );

        return false;
    }

    SetHandleInformation( stdinWrite, HANDLE_FLAG_INHERIT, 0 );
    SetHandleInformation( stdoutRead, HANDLE_FLAG_INHERIT, 0 );
    SetHandleInformation( stderrRead, HANDLE_FLAG_INHERIT, 0 );
    SetHandleInformation( controlWrite, HANDLE_FLAG_INHERIT, 0 );
    SetHandleInformation( eventRead, HANDLE_FLAG_INHERIT, 0 );

    STARTUPINFOW startup;
    ZeroMemory( &startup, sizeof( startup ) );
    startup.cb = sizeof( startup );
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdinRead;
    startup.hStdOutput = stdoutWrite;
    startup.hStdError = stderrWrite;

    PROCESS_INFORMATION processInfo;
    ZeroMemory( &processInfo, sizeof( processInfo ) );

    std::wstring command =
            quoteCommandArgument( m_InterpreterPath )
            + L" -m kisurf_ai.worker --stdio --control-handle "
            + std::to_wstring( reinterpret_cast<std::uintptr_t>( controlRead ) )
            + L" --event-handle "
            + std::to_wstring( reinterpret_cast<std::uintptr_t>( eventWrite ) );
    std::vector<wchar_t> commandLine( command.begin(), command.end() );
    commandLine.push_back( L'\0' );

    std::vector<wchar_t> environment = buildEnvironmentBlock( m_SdkRootPath );

    const BOOL created = CreateProcessW( nullptr, commandLine.data(), nullptr, nullptr,
                                         TRUE,
                                         CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                         environment.data(), nullptr, &startup,
                                         &processInfo );
    const DWORD errorCode = created ? 0 : GetLastError();

    closeHandle( stdinRead );
    closeHandle( stdoutWrite );
    closeHandle( stderrWrite );
    closeHandle( controlRead );
    closeHandle( eventWrite );

    if( !created )
    {
        closeHandle( stdinWrite );
        closeHandle( stdoutRead );
        closeHandle( stderrRead );
        closeHandle( controlWrite );
        closeHandle( eventRead );

        if( aError )
            *aError = wxS( "Could not launch persistent Python worker: " )
                      + windowsErrorText( errorCode );

        return false;
    }

    closeHandle( processInfo.hThread );

    m_Process = std::make_unique<AI_PYTHON_LOCAL_PROCESS>();
    m_Process->m_Process = processInfo.hProcess;
    m_Process->m_StdinWrite = stdinWrite;
    m_Process->m_StdoutRead = stdoutRead;
    m_Process->m_StderrRead = stderrRead;
    m_Process->m_ControlWrite = controlWrite;
    m_Process->m_EventRead = eventRead;
    m_Process->m_ProcessId = processInfo.dwProcessId;
    m_Process->m_EventThread =
            std::thread( &AI_PYTHON_LOCAL_WORKER::readEventFrames,
                         this, m_Process.get() );
    m_Pid = static_cast<long>( processInfo.dwProcessId );
    return true;
#else
    if( aError )
        *aError = wxS( "Persistent Python worker is only implemented on Windows." );

    return false;
#endif
}


bool AI_PYTHON_LOCAL_WORKER::writeRequestFrame( const std::string& aRequestPayload,
                                                wxString* aError )
{
#ifdef __WXMSW__
    if( !m_Process || !processIsRunning( m_Process.get() ) || !m_Process->m_StdinWrite )
    {
        if( aError )
            *aError = wxS( "Python worker process is not running." );

        return false;
    }

    std::string bytes = AI_PYTHON_WORKER_PROTOCOL::EncodeFrame( aRequestPayload );

    DWORD written = 0;
    const BOOL ok = WriteFile( m_Process->m_StdinWrite, bytes.data(),
                               static_cast<DWORD>( bytes.size() ), &written, nullptr );

    if( !ok || written != bytes.size() )
    {
        if( aError )
            *aError = wxS( "Could not write full request to Python worker." );

        return false;
    }

    return true;
#else
    wxUnusedVar( aRequestPayload );

    if( aError )
        *aError = wxS( "Persistent Python worker is only implemented on Windows." );

    return false;
#endif
}


void AI_PYTHON_LOCAL_WORKER::writeControlFrameBestEffort(
        const std::string& aRequestPayload )
{
#ifdef __WXMSW__
    if( !m_Process || !processIsRunning( m_Process.get() ) )
        return;

    HANDLE target = m_Process->m_ControlWrite ? m_Process->m_ControlWrite
                                              : m_Process->m_StdinWrite;

    if( !target )
        return;

    std::string bytes = AI_PYTHON_WORKER_PROTOCOL::EncodeFrame( aRequestPayload );
    DWORD written = 0;
    WriteFile( target, bytes.data(), static_cast<DWORD>( bytes.size() ), &written,
               nullptr );
#else
    wxUnusedVar( aRequestPayload );
#endif
}


bool AI_PYTHON_LOCAL_WORKER::readResponseFrame( std::string* aResponsePayload,
                                                wxString* aError )
{
    if( aResponsePayload )
        aResponsePayload->clear();

#ifdef __WXMSW__
    if( !m_Process || !m_Process->m_StdoutRead )
    {
        if( aError )
            *aError = wxS( "Python worker stdout is unavailable." );

        return false;
    }

    std::string       frame;
    std::atomic_bool  done{ false };
    std::atomic_bool  readOk{ false };
    std::atomic<DWORD> readError{ 0 };

    std::thread reader(
            [&]()
            {
                std::string header;

                for( ;; )
                {
                    char  ch = 0;
                    DWORD readCount = 0;

                    const BOOL ok = ReadFile( m_Process->m_StdoutRead, &ch, 1,
                                              &readCount, nullptr );

                    if( !ok || readCount == 0 )
                    {
                        readError.store( GetLastError() );
                        break;
                    }

                    if( ch == '\n' )
                        break;

                    if( ch != '\r' )
                        header.push_back( ch );
                }

                std::optional<size_t> payloadLength = parseFramePayloadLength( header );

                if( !payloadLength )
                {
                    readError.store( ERROR_INVALID_DATA );
                    done.store( true );
                    return;
                }

                frame = header + "\n";

                size_t remaining = *payloadLength;

                while( remaining > 0 )
                {
                    char  buffer[1024];
                    DWORD readCount = 0;
                    const DWORD toRead = static_cast<DWORD>(
                            std::min<size_t>( remaining, sizeof( buffer ) ) );

                    const BOOL ok = ReadFile( m_Process->m_StdoutRead, buffer, toRead,
                                              &readCount, nullptr );

                    if( !ok || readCount == 0 )
                    {
                        readError.store( GetLastError() );
                        break;
                    }

                    frame.append( buffer, readCount );
                    remaining -= readCount;
                }

                if( remaining == 0 )
                    readOk.store( true );

                done.store( true );
            } );

    wxStopWatch deadline;

    while( !done.load() && deadline.Time() < m_ResponseTimeoutMs )
    {
        if( !processIsRunning( m_Process.get() ) )
            break;

        wxMilliSleep( 5 );
    }

    if( !done.load() )
    {
        CancelSynchronousIo( static_cast<HANDLE>( reader.native_handle() ) );
        closeHandle( m_Process->m_StdoutRead );
        reader.join();
    }
    else
    {
        reader.join();
    }

    if( readOk.load() )
    {
        std::string payload;
        wxString frameError;

        if( !AI_PYTHON_WORKER_PROTOCOL::DecodeFrame( frame, &payload, &frameError ) )
        {
            if( aError )
                *aError = frameError;

            return false;
        }

        if( aResponsePayload )
            *aResponsePayload = payload;

        return true;
    }

    if( aError )
    {
        if( deadline.Time() >= m_ResponseTimeoutMs )
        {
            *aError = wxString::Format(
                    wxS( "Timed out waiting for Python worker response after %ld ms." ),
                    m_ResponseTimeoutMs );
        }
        else
        {
            *aError = wxS( "Python worker exited before writing a response." );
        }

        const DWORD errorCode = readError.load();

        if( errorCode != 0 && errorCode != ERROR_OPERATION_ABORTED
            && errorCode != ERROR_BROKEN_PIPE )
        {
            *aError += wxS( " Last stdout pipe error: " )
                       + windowsErrorText( errorCode );
        }
    }

    if( aResponsePayload && !frame.empty() )
        *aResponsePayload = frame;

    return false;
#else
    if( aError )
        *aError = wxS( "Persistent Python worker is only implemented on Windows." );

    return false;
#endif
}


void AI_PYTHON_LOCAL_WORKER::readEventFrames( AI_PYTHON_LOCAL_PROCESS* aProcess )
{
#ifdef __WXMSW__
    while( aProcess && !aProcess->m_StopEventReader.load() && aProcess->m_EventRead )
    {
        std::string frame;
        DWORD       readError = 0;

        if( !readFrameFromHandle( aProcess->m_EventRead, &frame, &readError ) )
        {
            wxUnusedVar( readError );
            break;
        }

        std::string payload;
        wxString    frameError;

        if( !AI_PYTHON_WORKER_PROTOCOL::DecodeFrame( frame, &payload, &frameError ) )
            continue;

        AI_PYTHON_EVENT event;
        wxString        eventError;

        if( !AI_PYTHON_WORKER_PROTOCOL::DecodeEvent( payload, &event, &eventError ) )
            continue;

        if( AI_PYTHON_EVENT_SINK* sink = m_EventSink )
            sink->OnPythonEvent( event );
    }
#else
    wxUnusedVar( aProcess );
#endif
}


void AI_PYTHON_LOCAL_WORKER::stopProcess( bool aHardKill )
{
#ifdef __WXMSW__
    if( !m_Process )
        return;

    if( processIsRunning( m_Process.get() ) )
    {
        if( !aHardKill && m_Process->m_StdinWrite )
        {
            const std::string shutdownFrame =
                    AI_PYTHON_WORKER_PROTOCOL::EncodeFrame(
                            AI_PYTHON_WORKER_PROTOCOL::EncodeShutdownRequest() );
            DWORD written = 0;
            WriteFile( m_Process->m_StdinWrite, shutdownFrame.data(),
                       static_cast<DWORD>( shutdownFrame.size() ), &written, nullptr );
            closeHandle( m_Process->m_StdinWrite );

            WaitForSingleObject( m_Process->m_Process, 2000 );
        }

        if( processIsRunning( m_Process.get() ) )
        {
            TerminateProcess( m_Process->m_Process, 1 );
            WaitForSingleObject( m_Process->m_Process, 2000 );
        }
    }

    m_Process->m_StopEventReader.store( true );

    if( m_Process->m_EventThread.joinable() )
    {
        CancelSynchronousIo( static_cast<HANDLE>( m_Process->m_EventThread.native_handle() ) );
        closeHandle( m_Process->m_EventRead );
        m_Process->m_EventThread.join();
    }

    drainHandle( m_Process->m_StdoutRead );
    drainHandle( m_Process->m_StderrRead );
    closeProcessHandles( m_Process.get() );
    m_Process.reset();
    m_Pid = 0;
    m_LastSessionId.store( 0 );
    m_CancelledSessionId.store( 0 );
    m_CellRunning.store( false );
    m_CancelRequested.store( false );
#else
    wxUnusedVar( aHardKill );
    m_Process.reset();
    m_Pid = 0;
    m_LastSessionId.store( 0 );
    m_CancelledSessionId.store( 0 );
    m_CellRunning.store( false );
    m_CancelRequested.store( false );
#endif
}
