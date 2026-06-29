#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_model_config.h>
#include <kisurf/ai/ai_types.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct KICOMMON_API AI_HTTP_HEADER
{
    wxString m_Name;
    wxString m_Value;
};

using AI_HTTP_STREAM_HANDLER = std::function<void( const std::string& )>;

struct KICOMMON_API AI_HTTP_REQUEST
{
    wxString                    m_Method;
    wxString                    m_Url;
    std::vector<AI_HTTP_HEADER> m_Headers;
    wxString                    m_Body;
    AI_HTTP_STREAM_HANDLER      m_StreamHandler;

    wxString HeaderValue( const wxString& aName ) const;
};

struct KICOMMON_API AI_HTTP_RESPONSE
{
    int      m_StatusCode = 0;
    wxString m_Body;
};

using AI_HTTP_HANDLER = std::function<bool( const AI_HTTP_REQUEST&, AI_HTTP_RESPONSE&, wxString& )>;

struct KICOMMON_API AI_PROVIDER_STREAM_EVENT
{
    uint64_t m_RequestId = 0;
    wxString m_TextDelta;
};

using AI_PROVIDER_STREAM_EVENT_SINK =
        std::function<void( const AI_PROVIDER_STREAM_EVENT& )>;

struct KICOMMON_API AI_PROVIDER_SETTINGS
{
    wxString m_BaseUrl;
    wxString m_ApiKey;
    wxString m_Model;

    bool HasApiKey() const;

    static wxString DefaultBaseUrl();
    static wxString DefaultModel();
};

class KICOMMON_API AI_PROVIDER
{
public:
    virtual ~AI_PROVIDER() = default;

    virtual AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) = 0;
    virtual AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest,
                                           AI_PROVIDER_STREAM_EVENT_SINK aStreamSink );
};

class KICOMMON_API AI_STUB_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override;
};

class KICOMMON_API AI_OPENAI_COMPAT_PROVIDER : public AI_PROVIDER
{
public:
    explicit AI_OPENAI_COMPAT_PROVIDER( AI_PROVIDER_SETTINGS aSettings );
    AI_OPENAI_COMPAT_PROVIDER( AI_PROVIDER_SETTINGS aSettings, AI_HTTP_HANDLER aHandler );

    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override;
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest,
                                   AI_PROVIDER_STREAM_EVENT_SINK aStreamSink ) override;

private:
    AI_PROVIDER_SETTINGS m_Settings;
    AI_HTTP_HANDLER      m_Handler;
};

KICOMMON_API std::unique_ptr<AI_PROVIDER> MakeDefaultAiProvider();
KICOMMON_API std::unique_ptr<AI_PROVIDER> MakeAiProviderFromModelConfig(
        const AI_MODEL_CONFIG& aConfig );
KICOMMON_API std::unique_ptr<AI_PROVIDER> MakeAiProviderFromModelConfig(
        const AI_MODEL_CONFIG& aConfig, AI_HTTP_HANDLER aHandler );
