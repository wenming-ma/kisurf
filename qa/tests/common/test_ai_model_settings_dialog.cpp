#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_model_settings_dialog_base.h>

#include <type_traits>
#include <wx/dialog.h>

BOOST_AUTO_TEST_SUITE( AiModelSettingsDialog )


class AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST : public AI_MODEL_SETTINGS_DIALOG_BASE
{
public:
    explicit AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST( wxWindow* aParent ) :
            AI_MODEL_SETTINGS_DIALOG_BASE( aParent )
    {
    }

    static constexpr bool HasProviderChoice()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::m_ProviderChoice )>;
    }

    static constexpr bool HasBaseUrl()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::m_BaseUrl )>;
    }

    static constexpr bool HasModel()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::m_Model )>;
    }

    static constexpr bool HasApiKey()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::m_ApiKey )>;
    }

    static constexpr bool HasResearchFolder()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::m_ResearchFolder )>;
    }

    static constexpr bool HasResearchFolderBrowseButton()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::m_ResearchFolderBrowse )>;
    }

    static constexpr bool HasHelpText()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::m_HelpText )>;
    }
};


BOOST_AUTO_TEST_CASE( DialogBaseExposesGeneratedSettingsSurface )
{
    BOOST_CHECK( ( std::is_base_of_v<wxDialog, AI_MODEL_SETTINGS_DIALOG_BASE> ) );
    BOOST_CHECK( AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::HasProviderChoice() );
    BOOST_CHECK( AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::HasBaseUrl() );
    BOOST_CHECK( AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::HasModel() );
    BOOST_CHECK( AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::HasApiKey() );
    BOOST_CHECK( AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::HasResearchFolder() );
    BOOST_CHECK(
            AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::HasResearchFolderBrowseButton() );
    BOOST_CHECK( AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::HasHelpText() );
}


BOOST_AUTO_TEST_SUITE_END()
