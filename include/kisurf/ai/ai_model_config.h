#pragma once

#include <kicommon.h>
#include <oauth/secure_token_store.h>

#include <memory>
#include <wx/string.h>

enum class AI_MODEL_PROVIDER_KIND
{
    OpenAiCompatible,
    AnthropicCompatible
};

struct KICOMMON_API AI_MODEL_CONFIG
{
    AI_MODEL_PROVIDER_KIND m_ProviderKind = AI_MODEL_PROVIDER_KIND::OpenAiCompatible;
    wxString               m_BaseUrl;
    wxString               m_Model;
    wxString               m_ApiKey;
    wxString               m_ResearchFolder;

    void Normalize();
};

KICOMMON_API wxString AiModelProviderKindToken( AI_MODEL_PROVIDER_KIND aKind );
KICOMMON_API wxString AiModelProviderKindLabel( AI_MODEL_PROVIDER_KIND aKind );
KICOMMON_API AI_MODEL_PROVIDER_KIND AiModelProviderKindFromToken( const wxString& aToken );

class KICOMMON_API AI_MODEL_CONFIG_STORE
{
public:
    AI_MODEL_CONFIG_STORE();
    AI_MODEL_CONFIG_STORE(
            wxString aPath,
            std::unique_ptr<OAUTH_SECRET_BACKEND> aSecretBackend =
                    std::make_unique<PLATFORM_SECRET_BACKEND>() );

    bool Load( AI_MODEL_CONFIG& aConfig, wxString* aError = nullptr ) const;
    bool Save( const AI_MODEL_CONFIG& aConfig, wxString* aError = nullptr ) const;

    const wxString& Path() const { return m_Path; }

    static wxString DefaultConfigPath();
    static AI_MODEL_CONFIG LoadUserConfig( wxString* aError = nullptr );
    static wxString SecretServiceName();
    static wxString SecretKeyForProvider( AI_MODEL_PROVIDER_KIND aKind );

private:
    wxString                              m_Path;
    std::unique_ptr<OAUTH_SECRET_BACKEND> m_SecretBackend;
};
