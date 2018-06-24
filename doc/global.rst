.. _global-namespace:

Global Namespace
================

.. index:: pair: namespace; global

.. toctree::
	:hidden:

	enum_Kit_HintType.rst
	enum_Kit_PlayerState.rst
	enum_Kit_StreamType.rst
	struct_Kit_Codec.rst
	struct_Kit_OutputFormat.rst
	struct_Kit_Player.rst
	struct_Kit_PlayerInfo.rst
	struct_Kit_PlayerStreamInfo.rst
	struct_Kit_Source.rst
	struct_Kit_SourceStreamInfo.rst
	struct_Kit_Version.rst



Overview
~~~~~~~~



.. _doxid-kitsource_8h_1aeee27e7a9aec5db8c436f9b654069f96:
.. _cid-kit_readcallback:
.. _doxid-kitsource_8h_1af1bf42b44a8adbb44f3c5cbdf5fb9994:
.. _cid-kit_seekcallback:
.. _doxid-kitlib_8h_1a06fc87d81c62e9abb8790b6e5713c55bac83d5d0a8047f9539b812b345e474514:
.. _cid-kit_font_hinting_none:
.. _doxid-kitlib_8h_1a06fc87d81c62e9abb8790b6e5713c55bad0e31158321ea49d89964fb97b790df7:
.. _cid-kit_font_hinting_light:
.. _doxid-kitlib_8h_1a06fc87d81c62e9abb8790b6e5713c55ba781efa23bf6f887cc48f2413e9fc100b:
.. _cid-kit_font_hinting_normal:
.. _doxid-kitlib_8h_1a06fc87d81c62e9abb8790b6e5713c55ba97b38fff75460ebb01550e49fe4216a4:
.. _cid-kit_font_hinting_native:
.. _doxid-kitlib_8h_1a06fc87d81c62e9abb8790b6e5713c55baa7ed6dce828d3ae8c6103f3fae788c7f:
.. _cid-kit_font_hinting_count:
.. _doxid-kitlib_8h_1adf764cbdea00d65edcd07bb9953ad2b7a22ca5f271135f2d71ce2ab22a41edbfa:
.. _cid-kit_init_network:
.. _doxid-kitlib_8h_1adf764cbdea00d65edcd07bb9953ad2b7a62c9dc6b5a2b836dccc78b3de7bb5a25:
.. _cid-kit_init_ass:
.. _doxid-kiterror_8h_1aa87517d3272e56a664be753f983a105e:
.. _cid-kit_geterror:
.. _doxid-kiterror_8h_1a142e957051a9255cd1163f75f390e09e:
.. _cid-kit_seterror:
.. _doxid-kiterror_8h_1abb81d1212de0be17b9d3d6360808ee07:
.. _cid-kit_clearerror:
.. _doxid-kitlib_8h_1aaf1aa9267665ec110fce23bdefe3b859:
.. _cid-kit_init:
.. _doxid-kitlib_8h_1a61597347333fdb5a32ad6a0c0a56bff2:
.. _cid-kit_quit:
.. _doxid-kitlib_8h_1a381a1975f68a9f38a84079a375ba7a37:
.. _cid-kit_sethint:
.. _doxid-kitlib_8h_1a00a081690155922333437007e5a8847e:
.. _cid-kit_gethint:
.. _doxid-kitlib_8h_1abccf63b4ede524b6a983ae2f4314cd3a:
.. _cid-kit_getversion:
.. _doxid-kitplayer_8h_1ae289e32c4b7247f1d9745f0efe69be88:
.. _cid-kit_createplayer:
.. _doxid-kitplayer_8h_1a11f15d871f0f00cfd317d7d7631bddc7:
.. _cid-kit_closeplayer:
.. _doxid-kitplayer_8h_1a37c2594af61d78283b0b0931327ab8c6:
.. _cid-kit_setplayerscreensize:
.. _doxid-kitplayer_8h_1a1c5af5c46faa6146910a4c475a099b6a:
.. _cid-kit_getplayervideostream:
.. _doxid-kitplayer_8h_1a65cacfbd2473753b5da5ba4b8c11cb57:
.. _cid-kit_getplayeraudiostream:
.. _doxid-kitplayer_8h_1a27eac12c57193ea1df5cab9fa3ad05a1:
.. _cid-kit_getplayersubtitlestream:
.. _doxid-kitplayer_8h_1ada1f6861f415600a168667b03d665bf0:
.. _cid-kit_updateplayer:
.. _doxid-kitplayer_8h_1ab624ca871d996116327cde4d7d171f86:
.. _cid-kit_getplayervideodata:
.. _doxid-kitplayer_8h_1a9da1e6368fb42bee61170ba5259b55e9:
.. _cid-kit_getplayersubtitledata:
.. _doxid-kitplayer_8h_1a396d9e6611a79071730b18cbf8d43a75:
.. _cid-kit_getplayeraudiodata:
.. _doxid-kitplayer_8h_1a1dffc4e565f60fec223032b3c52bc6f5:
.. _cid-kit_getplayerinfo:
.. _doxid-kitplayer_8h_1a7e9c3b0f5ced1ad65336f3c7543147d8:
.. _cid-kit_getplayerstate:
.. _doxid-kitplayer_8h_1aff4b23fded79add9ee40df9ee17ca5fd:
.. _cid-kit_playerplay:
.. _doxid-kitplayer_8h_1ae05f61f1246c7845527fac2647b3e2f1:
.. _cid-kit_playerstop:
.. _doxid-kitplayer_8h_1a30378eb20bb95f9a13c83674767e9539:
.. _cid-kit_playerpause:
.. _doxid-kitplayer_8h_1a13687f4ec1a0ea21fa1ce6387107cfa6:
.. _cid-kit_playerseek:
.. _doxid-kitplayer_8h_1a721a87d0bdad1f16d8e32d75b9598a67:
.. _cid-kit_getplayerduration:
.. _doxid-kitplayer_8h_1a3083744312ce3b37e63be1e47151d1a4:
.. _cid-kit_getplayerposition:
.. _doxid-kitsource_8h_1a2033cf689466faa2aa2c61bed2de3f3e:
.. _cid-kit_createsourcefromurl:
.. _doxid-kitsource_8h_1a4674541bc8c95db9500c8ac5a09149fe:
.. _cid-kit_createsourcefromcustom:
.. _doxid-kitsource_8h_1ae5b6e651789b308c594f7040a6e6bd2e:
.. _cid-kit_closesource:
.. _doxid-kitsource_8h_1ab832e7a5db41f73af3ec849f6ff7bbaa:
.. _cid-kit_getsourcestreaminfo:
.. _doxid-kitsource_8h_1a1a8ac4a412646ca5b445e0e75165fd1f:
.. _cid-kit_getsourcestreamcount:
.. _doxid-kitsource_8h_1afd339c3956193700466eddd51ddc7b70:
.. _cid-kit_getbestsourcestream:
.. _doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac:
.. _cid-kit_api:
.. _doxid-kitcodec_8h_1a403358e3731b03abe43227b7d0bba494:
.. _cid-kit_codec_desc_max:
.. _doxid-kitcodec_8h_1a8df06194dd3ad7073060f3ffe5d95ee4:
.. _cid-kit_codec_name_max:
.. _doxid-kitconfig_8h_1af58c772c6f0da16b8258b23bd2253ec5:
.. _cid-kit_dll_export:
.. _doxid-kitconfig_8h_1a6a6ecbe120951240795535a3622c3d4c:
.. _cid-kit_dll_import:
.. _doxid-kitconfig_8h_1af4944ba06c64765f9066a689e74fe13b:
.. _cid-kit_dll_local:
.. _doxid-kitconfig_8h_1aad54d0729497924f16c2e4ffa87c9509:
.. _cid-kit_local:
.. ref-code-block:: cpp
	:class: overview-code-block

	// typedefs

	typedef int (* Kit_ReadCallback)(
	    void *,
	    uint8_t *,
	    int
	    )

	typedef int64_t (* Kit_SeekCallback)(
	    void *,
	    int64_t,
	    int
	    )

	// enums

	enum
	{
	    KIT_FONT_HINTING_NONE = 0
	    KIT_FONT_HINTING_LIGHT 
	    KIT_FONT_HINTING_NORMAL 
	    KIT_FONT_HINTING_NATIVE 
	    KIT_FONT_HINTING_COUNT 
	}

	enum
	{
	    KIT_INIT_NETWORK = 0x1
	    KIT_INIT_ASS = 0x2
	}

	enum :ref:`Kit_HintType<doxid-kitlib_8h_1ad71bf7853ac2e3a7f98bf9638a58844e>`
	enum :ref:`Kit_PlayerState<doxid-kitplayer_8h_1a02988a97b08c9a74fed9009f64e6a1b0>`
	enum :ref:`Kit_StreamType<doxid-kitsource_8h_1af54654f42d6733aacfe0f6a95379d9c2>`

	// structs

	struct :ref:`Kit_Codec<doxid-struct_kit___codec>` 
	struct :ref:`Kit_OutputFormat<doxid-struct_kit___output_format>` 
	struct :ref:`Kit_Player<doxid-struct_kit___player>` 
	struct :ref:`Kit_PlayerInfo<doxid-struct_kit___player_info>` 
	struct :ref:`Kit_PlayerStreamInfo<doxid-struct_kit___player_stream_info>` 
	struct :ref:`Kit_Source<doxid-struct_kit___source>` 
	struct :ref:`Kit_SourceStreamInfo<doxid-struct_kit___source_stream_info>` 
	struct :ref:`Kit_Version<doxid-struct_kit___version>` 

	// global functions

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` const char* Kit_GetError ()

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` void Kit_SetError (
	    const char* fmt,
	    ...
	    )

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` void Kit_ClearError ()
	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` int Kit_Init (unsigned int flags)
	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` void Kit_Quit ()

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` void Kit_SetHint (
	    :ref:`Kit_HintType<doxid-kitlib_8h_1ad71bf7853ac2e3a7f98bf9638a58844e>` type,
	    int value
	    )

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` int Kit_GetHint (:ref:`Kit_HintType<doxid-kitlib_8h_1ad71bf7853ac2e3a7f98bf9638a58844e>` type)
	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` void Kit_GetVersion (:ref:`Kit_Version<doxid-struct_kit___version>`* version)

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` :ref:`Kit_Player<doxid-struct_kit___player>`* Kit_CreatePlayer (
	    const :ref:`Kit_Source<doxid-struct_kit___source>`* src,
	    int video_stream_index,
	    int audio_stream_index,
	    int subtitle_stream_index,
	    int screen_w,
	    int screen_h
	    )

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` void Kit_ClosePlayer (:ref:`Kit_Player<doxid-struct_kit___player>`* player)

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` void Kit_SetPlayerScreenSize (
	    :ref:`Kit_Player<doxid-struct_kit___player>`* player,
	    int w,
	    int h
	    )

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` int Kit_GetPlayerVideoStream (const :ref:`Kit_Player<doxid-struct_kit___player>`* player)
	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` int Kit_GetPlayerAudioStream (const :ref:`Kit_Player<doxid-struct_kit___player>`* player)
	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` int Kit_GetPlayerSubtitleStream (const :ref:`Kit_Player<doxid-struct_kit___player>`* player)
	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` int Kit_UpdatePlayer (:ref:`Kit_Player<doxid-struct_kit___player>`* player)

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` int Kit_GetPlayerVideoData (
	    :ref:`Kit_Player<doxid-struct_kit___player>`* player,
	    SDL_Texture* texture
	    )

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` int Kit_GetPlayerSubtitleData (
	    :ref:`Kit_Player<doxid-struct_kit___player>`* player,
	    SDL_Texture* texture,
	    SDL_Rect* sources,
	    SDL_Rect* targets,
	    int limit
	    )

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` int Kit_GetPlayerAudioData (
	    :ref:`Kit_Player<doxid-struct_kit___player>`* player,
	    unsigned char* buffer,
	    int length
	    )

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` void Kit_GetPlayerInfo (
	    const :ref:`Kit_Player<doxid-struct_kit___player>`* player,
	    :ref:`Kit_PlayerInfo<doxid-struct_kit___player_info>`* info
	    )

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` :ref:`Kit_PlayerState<doxid-kitplayer_8h_1a02988a97b08c9a74fed9009f64e6a1b0>` Kit_GetPlayerState (const :ref:`Kit_Player<doxid-struct_kit___player>`* player)
	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` void Kit_PlayerPlay (:ref:`Kit_Player<doxid-struct_kit___player>`* player)
	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` void Kit_PlayerStop (:ref:`Kit_Player<doxid-struct_kit___player>`* player)
	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` void Kit_PlayerPause (:ref:`Kit_Player<doxid-struct_kit___player>`* player)

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` int Kit_PlayerSeek (
	    :ref:`Kit_Player<doxid-struct_kit___player>`* player,
	    double time
	    )

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` double Kit_GetPlayerDuration (const :ref:`Kit_Player<doxid-struct_kit___player>`* player)
	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` double Kit_GetPlayerPosition (const :ref:`Kit_Player<doxid-struct_kit___player>`* player)
	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` :ref:`Kit_Source<doxid-struct_kit___source>`* Kit_CreateSourceFromUrl (const char* path)

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` :ref:`Kit_Source<doxid-struct_kit___source>`* Kit_CreateSourceFromCustom (
	    :ref:`Kit_ReadCallback<doxid-kitsource_8h_1aeee27e7a9aec5db8c436f9b654069f96>` read_cb,
	    :ref:`Kit_SeekCallback<doxid-kitsource_8h_1af1bf42b44a8adbb44f3c5cbdf5fb9994>` seek_cb,
	    void* userdata
	    )

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` void Kit_CloseSource (:ref:`Kit_Source<doxid-struct_kit___source>`* src)

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` int Kit_GetSourceStreamInfo (
	    const :ref:`Kit_Source<doxid-struct_kit___source>`* src,
	    :ref:`Kit_SourceStreamInfo<doxid-struct_kit___source_stream_info>`* info,
	    int index
	    )

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` int Kit_GetSourceStreamCount (const :ref:`Kit_Source<doxid-struct_kit___source>`* src)

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` int Kit_GetBestSourceStream (
	    const :ref:`Kit_Source<doxid-struct_kit___source>`* src,
	    const :ref:`Kit_StreamType<doxid-kitsource_8h_1af54654f42d6733aacfe0f6a95379d9c2>` type
	    )

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` const char* :ref:`Kit_GetSDLAudioFormatString<doxid-kitutils_8h_1af0fd2df7dded6af732b481209d004b26>` (unsigned int type)
	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` const char* :ref:`Kit_GetSDLPixelFormatString<doxid-kitutils_8h_1a553ebe72950a7687874f9a0f5b737138>` (unsigned int type)
	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` const char* :ref:`Kit_GetKitStreamTypeString<doxid-kitutils_8h_1ac8e439b66934625d4d46c660d111fedd>` (unsigned int type)

	// macros

	#define KIT_API
	#define KIT_CODEC_DESC_MAX
	#define KIT_CODEC_NAME_MAX
	#define KIT_DLL_EXPORT
	#define KIT_DLL_IMPORT
	#define KIT_DLL_LOCAL
	#define KIT_LOCAL

.. _details-doxid-global:

Detailed Documentation
~~~~~~~~~~~~~~~~~~~~~~



Global Functions
----------------

.. _doxid-kitutils_8h_1af0fd2df7dded6af732b481209d004b26:
.. _cid-kit_getsdlaudioformatstring:
.. ref-code-block:: cpp
	:class: title-code-block

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` const char* Kit_GetSDLAudioFormatString (unsigned int type)

Returns a descriptive string for SDL audio format types.



.. rubric:: Parameters:

.. list-table::
    :widths: 20 80

    *
        - type

        - SDL_AudioFormat



.. rubric:: Returns:

Format string, eg. "AUDIO_S8".

.. _doxid-kitutils_8h_1a553ebe72950a7687874f9a0f5b737138:
.. _cid-kit_getsdlpixelformatstring:
.. ref-code-block:: cpp
	:class: title-code-block

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` const char* Kit_GetSDLPixelFormatString (unsigned int type)

Returns a descriptive string for SDL pixel format types.



.. rubric:: Parameters:

.. list-table::
    :widths: 20 80

    *
        - type

        - SDL_PixelFormat



.. rubric:: Returns:

Format string, eg. "SDL_PIXELFORMAT_YV12"

.. _doxid-kitutils_8h_1ac8e439b66934625d4d46c660d111fedd:
.. _cid-kit_getkitstreamtypestring:
.. ref-code-block:: cpp
	:class: title-code-block

	:ref:`KIT_API<doxid-kitconfig_8h_1ab1c036a10f5a265f155e61f6e4b53eac>` const char* Kit_GetKitStreamTypeString (unsigned int type)

Returns a descriptibe string for Kitchensink stream types.



.. rubric:: Parameters:

.. list-table::
    :widths: 20 80

    *
        - type

        - Kit_StreamType



.. rubric:: Returns:

Format string, eg. "KIT_STREAMTYPE_VIDEO"

