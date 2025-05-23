file(GLOB_RECURSE ARB_SHADERS  CONFIGURE_DEPENDS "shaders/ARB/*.vp" "shaders/ARB/*.fp")
file(GLOB_RECURSE GLSL_SHADERS CONFIGURE_DEPENDS "shaders/GLSL/*.glsl" "shaders/GLSL/*.lua")

foreach(ITEM ${ARB_SHADERS})
   get_filename_component(ITEM_NAME ${ITEM} NAME)
   list(APPEND FILES "shaders/ARB/${ITEM_NAME}")
endforeach()

foreach(ITEM ${GLSL_SHADERS})
   get_filename_component(ITEM_NAME ${ITEM} NAME)
   list(APPEND FILES "shaders/GLSL/${ITEM_NAME}")
endforeach()

list(APPEND FILES
	unittextures/tatex/PALETTE.PAL
	unittextures/tree_fir_tall_5_1.dds
	unittextures/tree_fir_tall_5_2.dds
	unittextures/tree_fir_tall_5_normal.dds
	mapgenerator/mapinfo_template.lua
	objects3d/fir_tree_large.s3o
	objects3d/fir_tree_medium.s3o
	objects3d/fir_tree_small.s3o
	objects3d/fir_tree_smallest.s3o
	features/treetype.lua
	EngineOptions.lua
	gamedata/unitdefs.lua
	gamedata/explosion_alias.lua
	gamedata/unitdefs_post.lua
	gamedata/armordefs.lua
	gamedata/explosions.lua
	gamedata/system.lua
	gamedata/scanutils.lua
	gamedata/parse_snd.lua
	gamedata/parse_tdf.lua
	gamedata/weapondefs.lua
	gamedata/defs.lua
	gamedata/defs_post.lua
	gamedata/weapondefs_post.lua
	gamedata/modrules.lua
	gamedata/resources.lua
	gamedata/featuredefs.lua
	gamedata/download_builds.lua
	gamedata/sidedata.lua
	gamedata/featuredefs_post.lua
	gamedata/movedefs.lua
	gamedata/unit_script_header.lua
	gamedata/VFSUtils.lua
	gamedata/parse_fbi.lua
	gamedata/resources_map.lua
	gamedata/sounds.lua
	gamedata/icontypes.lua
	anims/cursornormal_0.bmp
	anims/cursortime_0.png
	anims/cursordwatch_2.png
	anims/cursordwatch.txt
	anims/cursorwait_3.png
	anims/cursornumber_00.png
	anims/cursorrestore_2.png
	anims/cursornumber_07.png
	anims/cursorcentroid_0.png
	anims/cursornumber_11.png
	anims/cursorselfd_0.png
	anims/cursorgather_1.png
	anims/cursornumber.txt
	anims/cursordwatch_1.png
	anims/cursorrestore_3.png
	anims/cursorselfd_1.png
	anims/cursornumber_06.png
	anims/cursorselfd.txt
	anims/cursornumber_09.png
	anims/cursornumber_05.png
	anims/cursorbuildgood_0.png
	anims/cursorrestore.txt
	anims/cursorwait_1.png
	anims/cursorgather_0.png
	anims/cursorgather_2.png
	anims/cursornumber_02.png
	anims/cursornumber_04.png
	anims/cursorgather.txt
	anims/cursorwait_0.png
	anims/cursornumber_10.png
	anims/cursorrestore_0.png
	anims/cursorbuildbad_0.png
	anims/cursorgather_3.png
	anims/cursorwait.txt
	anims/cursordwatch_0.png
	anims/cursorselfd_2.png
	anims/cursorwait_2.png
	anims/cursorrestore_1.png
	anims/cursornumber_08.png
	anims/cursorrestore_4.png
	anims/cursornumber_01.png
	anims/cursornumber_03.png
	modinfo.lua
	bitmaps/icons/frame_slate_faded_128x96.png
	bitmaps/icons/frame_slate_128x96.png
	bitmaps/icons/frame.png
	bitmaps/icons/frame_slate_hilite_128x96.png
	bitmaps/minimapbuttons.png
	LuaGadgets/callins.lua
	LuaGadgets/Gadgets/share_levels.lua
	LuaGadgets/Gadgets/cmd_nocost.lua
	LuaGadgets/Gadgets/game_end.lua
	LuaGadgets/Gadgets/share_delayed.lua
	LuaGadgets/Gadgets/README.txt
	LuaGadgets/Gadgets/share_no_builders.lua
	LuaGadgets/Gadgets/object_statusbars_default.lua
	LuaGadgets/Gadgets/cmd_doline.lua
	LuaGadgets/Gadgets/share_control.lua
	LuaGadgets/Gadgets/unit_script.lua
	LuaGadgets/Gadgets/game_spawn.lua
	LuaGadgets/Gadgets/unit_limit_dgun.lua
	LuaGadgets/system.lua
	LuaGadgets/actions.lua
	LuaGadgets/README.txt
	LuaGadgets/colors.h.lua
	LuaGadgets/setupdefs.lua
	LuaGadgets/gadgets.lua
	LuaHandler/config.lua
	LuaHandler/doc/gpl-2.0.txt
	LuaHandler/doc/changelog.txt
	LuaHandler/handler.lua
	LuaHandler/SystemAddons/watchdog.lua
	LuaHandler/Utilities/vector.lua
	LuaHandler/Utilities/setupdefs.lua
	LuaHandler/Utilities/ctrlpanel.lua
	LuaHandler/Utilities/keysym.lua
	LuaHandler/Utilities/list.lua
	LuaHandler/Utilities/cache.lua
	LuaHandler/Utilities/colors.lua
	LuaHandler/Utilities/table.lua
	LuaHandler/Utilities/savetable.lua
	LuaHandler/Utilities/VFS_GetFileChecksum.lua
	LuaHandler/Utilities/utils.lua
	LuaHandler/Utilities/crashHandler.lua
	LuaHandler/Utilities/specialCallinHandlers.lua
	LuaHandler/Utilities/actions.lua
	LuaHandler/Utilities/addonRevisions.lua
	LuaIntro/config.lua
	LuaIntro/main.lua
	LuaIntro/Addons/bg_logo.lua
	LuaIntro/Addons/loadprogress.lua
	LuaIntro/Addons/main.lua
	LuaIntro/Addons/music.lua
	LuaIntro/Addons/bg_texture.lua
	LuaSocket/socket.lua
)

create_base_content_archive("base" "springcontent.sdz" "${FILES}")

