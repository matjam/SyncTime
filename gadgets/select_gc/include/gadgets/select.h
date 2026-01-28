

#ifndef GADGETS_SELECT_H
#define GADGETS_SELECT_H


/* $VER: select.h 40.17 (7.6.2000) */
/* © 2000 by Massimo Tantignone    */


/* Public definitions for the "select gadget" BOOPSI class */

#define SGA_Dummy       (TAG_USER + 0xA0000)
#define SGA_Active      (SGA_Dummy + 0x0001)
#define SGA_Labels      (SGA_Dummy + 0x0002)
#define SGA_MinItems    (SGA_Dummy + 0x0003)
#define SGA_FullPopUp   (SGA_Dummy + 0x0004)
#define SGA_PopUpDelay  (SGA_Dummy + 0x0005)
#define SGA_PopUpPos    (SGA_Dummy + 0x0006)
#define SGA_Sticky      (SGA_Dummy + 0x0007)
#define SGA_TextAttr    (SGA_Dummy + 0x0008)
#define SGA_TextFont    (SGA_Dummy + 0x0009)
#define SGA_TextPlace   (SGA_Dummy + 0x000A)
#define SGA_Underscore  (SGA_Dummy + 0x000B)
#define SGA_Justify     (SGA_Dummy + 0x000C)
#define SGA_Quiet       (SGA_Dummy + 0x000D)
#define SGA_Symbol      (SGA_Dummy + 0x000E)
#define SGA_SymbolWidth (SGA_Dummy + 0x000F)
#define SGA_SymbolOnly  (SGA_Dummy + 0x0010)
#define SGA_Separator   (SGA_Dummy + 0x0011)
#define SGA_ListFrame   (SGA_Dummy + 0x0012)
#define SGA_DropShadow  (SGA_Dummy + 0x0013)
#define SGA_ItemHeight  (SGA_Dummy + 0x0014)
#define SGA_ListJustify (SGA_Dummy + 0x0015)
#define SGA_ActivePens  (SGA_Dummy + 0x0016)
#define SGA_ActiveBox   (SGA_Dummy + 0x0017)
#define SGA_BorderSize  (SGA_Dummy + 0x0018)
#define SGA_FullWidth   (SGA_Dummy + 0x0019)
#define SGA_FollowMode  (SGA_Dummy + 0x001A)
#define SGA_ReportAll   (SGA_Dummy + 0x001B)
#define SGA_Refresh     (SGA_Dummy + 0x001C)
#define SGA_ItemSpacing (SGA_Dummy + 0x001D)
#define SGA_MinTime     (SGA_Dummy + 0x001E)  /* Min anim duration (40.14) */
#define SGA_MaxTime     (SGA_Dummy + 0x001F)  /* Max anim duration (40.14) */
#define SGA_PanelMode   (SGA_Dummy + 0x0020)  /* Window? Blocking? (40.14) */
#define SGA_Transparent (SGA_Dummy + 0x0021)  /* Transparent menu? (40.17) */

#define SGJ_LEFT   0
#define SGJ_CENTER 1
#define SGJ_RIGHT  2

#define SGPOS_ONITEM 0
#define SGPOS_ONTOP  1
#define SGPOS_BELOW  2
#define SGPOS_RIGHT  3

#define SGFM_NONE 0
#define SGFM_KEEP 1
#define SGFM_FULL 2

#define SGPM_WINDOW    0
#define SGPM_DIRECT_NB 1
#define SGPM_DIRECT_B  2

#define SGS_NOSYMBOL ((APTR)0xFFFFFFFF)


/* Public structures for the "select gadget" BOOPSI class */


#endif


