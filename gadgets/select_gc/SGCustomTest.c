

/*  Select.gadget test 2 (15.1.2001)  */
/*  Written for SAS/C                 */
/*  Compile: SC LINK SGCustomTest     */
/*  © 2001 Massimo Tantignone         */


#include "exec/types.h"
#include "exec/libraries.h"
#include "dos/dos.h"
#include "intuition/intuition.h"
#include "intuition/gadgetclass.h"
#include "libraries/gadtools.h"
#include "proto/intuition.h"
#include "proto/gadtools.h"
#include "proto/exec.h"

#include <gadgets/select.h>
#include <clib/selectgadget_protos.h>
#include <pragmas/selectgadget_pragmas.h>


/* The library base for the "select.gadget" class library */

struct Library *SelectGadgetBase;


ULONG main(void)
{
   /* The usual stuff */

   struct Screen *scr;
   struct Window *win;
   struct IntuiMessage *imsg;
   struct DrawInfo *dri;
   APTR vi;
   ULONG class, code, done = FALSE;
   ULONG width = 640, height = 200;
   ULONG one = FALSE, two = FALSE, three = FALSE;
   struct NewGadget ng;
   struct Gadget *gad1, *gad2, *gad3, *glist = NULL;
   struct Gadget *iaddress;
   UBYTE msgbuffer[40], versbuffer[40];
   STRPTR labels1[] = { "First option",
                        "Second option",
                        "Third option",
                        "Fourth option",
                        NULL };
   STRPTR labels2[] = { "This is a",
                        "GadTools gadget",
                        "which was made",
                        "pop-up",
                        "by the support",
                        "functions of",
                        "the select.gadget",
                        "library.",
                        NULL };

   /* Let's try to open the "select.gadget" library any way we can */

   SelectGadgetBase = OpenLibrary("Classes/Gadgets/select.gadget",40L);

   if (!SelectGadgetBase)
      SelectGadgetBase = OpenLibrary("Gadgets/select.gadget",40L);

   if (!SelectGadgetBase)
      SelectGadgetBase = OpenLibrary("select.gadget",40L);

   /* Really not found? Then quit (and complain a bit) */

   if (!SelectGadgetBase) return (RETURN_FAIL);

   /* Also quit (with an error message) if the library we found is too old */

   if ((SelectGadgetBase->lib_Version == 40) &&
       (SelectGadgetBase->lib_Revision < 18))
   {
      struct EasyStruct es = { sizeof(struct EasyStruct),0L,
                               "SGCustomTest","%s"," Ok " };

      EasyRequest(NULL,&es,NULL,
                  "An older version of select.gadget is\n" \
                  "already in use. This example program\n" \
                  "cannot work correctly. Please flush\n" \
                  "your libraries and try again.");

      CloseLibrary(SelectGadgetBase);
      return (RETURN_FAIL);
   }

   /* Let the user know what we are using exactly */

   sprintf(versbuffer,"Using select.gadget %ld.%ld",
           SelectGadgetBase->lib_Version,SelectGadgetBase->lib_Revision);

   /* Inquire about the real screen size */

   if (scr = LockPubScreen(NULL))
   {
      width = scr->Width;
      height = scr->Height;
      UnlockPubScreen(NULL,scr);
   }

   /* Open a window on the default public screen */

   if (win = OpenWindowTags(NULL,WA_Left,(width - 500) / 2,
                                 WA_Top,(height - 160) / 2,
                                 WA_Width,500,WA_Height,160,
                                 WA_MinWidth,100,WA_MinHeight,100,
                                 WA_MaxWidth,-1,WA_MaxHeight,-1,
                                 WA_CloseGadget,TRUE,
                                 WA_SizeGadget,TRUE,
                                 WA_DepthGadget,TRUE,
                                 WA_DragBar,TRUE,
                                 WA_SimpleRefresh,TRUE,
                                 WA_Activate,TRUE,
                                 WA_Title,"select.gadget custom gadget test",
                                 WA_IDCMP,IDCMP_CLOSEWINDOW |
                                          IDCMP_GADGETUP |
                                          IDCMP_VANILLAKEY |
                                          IDCMP_MOUSEBUTTONS |
                                          IDCMP_REFRESHWINDOW,
                                 TAG_END))
   {
      /* Get the screen's DrawInfo, it will be useful... */

      if (dri = GetScreenDrawInfo(win->WScreen))
      {
         /* Same for the VisualInfo */

         if (vi = GetVisualInfoA(win->WScreen,NULL))
         {
            /* Create three gadgets, the GadTools way */

            glist = CreateContext(&glist);

            /* The width isn't very accurate, but this is just an example */

            ng.ng_LeftEdge = 40;
            ng.ng_TopEdge = win->BorderTop + 30;
            ng.ng_Width = win->WScreen->RastPort.Font->tf_XSize * 18 + 30;
            ng.ng_Height = win->WScreen->Font->ta_YSize + 6;
            ng.ng_GadgetText = "G_adTools 1";
            ng.ng_TextAttr = win->WScreen->Font;
            ng.ng_GadgetID = 1;
            ng.ng_Flags = 0L;
            ng.ng_VisualInfo = vi;

            gad1 = CreateGadget(GENERIC_KIND,glist,&ng,GT_Underscore,'_',
                                                       TAG_END);

            ng.ng_LeftEdge = win->Width - 40 - ng.ng_Width;
            ng.ng_TopEdge += 32;
            ng.ng_GadgetText = "Ga_dTools 2";
            ng.ng_GadgetID = 2;

            gad2 = CreateGadget(GENERIC_KIND,gad1,&ng,GT_Underscore,'_',
                                                      TAG_END);

            ng.ng_LeftEdge = win->BorderLeft + 1;
            /* TopEdge and Width will be changed later */
            ng.ng_GadgetText = NULL;
            ng.ng_GadgetID = 3;

            gad3 = CreateGadget(GENERIC_KIND,gad2,&ng,TAG_END);

            /* If all went ok, transform the gadgets and use them */

            if (gad3)
            {
               one = InitSelectGadget(gad1,0L,SGA_TextPlace,PLACETEXT_RIGHT,
                                              SGA_Labels,labels1,
                                              SGA_DropShadow,TRUE,
                                              SGA_FollowMode,SGFM_KEEP,
                                              SGA_Transparent,TRUE,
                                              TAG_END);

               two = InitSelectGadget(gad2,0L,SGA_TextPlace,PLACETEXT_LEFT,
                                              SGA_Labels,labels2,
                                              SGA_Active,3,
                                              SGA_ItemSpacing,2,
                                              SGA_PopUpPos,SGPOS_BELOW,
                                              SGA_SymbolWidth,-21,
                                              TAG_END);

               three = InitSelectGadget(gad3,0L,GA_ReadOnly,TRUE,
                                                TAG_END);

               /* Pos/size must be changed manually for non-BOOPSI gadgets */

               gad3->TopEdge = -(gad3->Height + win->BorderBottom);
               gad3->Width = -(win->BorderLeft + win->BorderRight + 2);
               ((struct ExtGadget *)gad3)->BoundsTopEdge = gad3->TopEdge;
               ((struct ExtGadget *)gad3)->BoundsWidth = gad3->Width;
               gad3->Flags |= (GFLG_RELBOTTOM | GFLG_RELWIDTH);

               /* The same holds true for text, when it's not an IntuiText */

               gad3->GadgetText = (APTR)versbuffer;
               gad3->Flags |= GFLG_LABELSTRING;

               /* Add the gadgets to the window and display them */

               AddGList(win,glist,-1,-1,NULL);
               RefreshGList(glist,win,NULL,-1);
               GT_RefreshWindow(win,NULL);
            }

            /* Now let's handle the events until the window gets closed */

            while (!done)
            {
               Wait(1 << win->UserPort->mp_SigBit);

               while (imsg = GT_GetIMsg(win->UserPort))
               {
                  class = imsg->Class;
                  code = imsg->Code;
                  iaddress = imsg->IAddress;
                  GT_ReplyIMsg(imsg);

                  if (class == IDCMP_CLOSEWINDOW) done = TRUE;

                  else if (class == IDCMP_GADGETUP)
                  {
                     if (gad3)
                     {
                        sprintf(msgbuffer,"Gadget: %ld, Item: %ld",
                                           iaddress->GadgetID,
                                           code);

                        gad3->GadgetText = (APTR)msgbuffer;
                        SetSGAttrs(gad3,win,NULL,0,SGA_Refresh,TRUE,TAG_END);
                     }
                  }

                  else if (class == IDCMP_VANILLAKEY)
                  {
                     switch (toupper(code))
                     {
                        case 'A': iaddress = gad1; break;
                        case 'D': iaddress = gad2; break;
                        default: iaddress = NULL;
                     }

                     if (iaddress)
                     {
                        ActivateGadget(iaddress,win,NULL);
                        sprintf(msgbuffer,"Key: %lc",code);
                        gad3->GadgetText = (APTR)msgbuffer;
                        SetSGAttrs(gad3,win,NULL,0,SGA_Refresh,TRUE,TAG_END);
                     }
                  }

                  else if ((class == IDCMP_MOUSEBUTTONS) &&
                           (code == SELECTDOWN))
                  {
                     gad3->GadgetText = (APTR)versbuffer;
                     SetSGAttrs(gad3,win,NULL,0,SGA_Refresh,TRUE,TAG_END);
                  }

                  else if (class == IDCMP_REFRESHWINDOW)
                  {
                     GT_BeginRefresh(win);
                     GT_EndRefresh(win,TRUE);
                  }
               }
            }

            /* If the gadgets were added, remove them */

            if (gad3)
            {
               RemoveGList(win,glist,-1);
            }

            /* Strip the gadgets of additional "select" information */

            if (one) ClearSelectGadget(gad1);
            if (two) ClearSelectGadget(gad2);
            if (three) ClearSelectGadget(gad3);

            /* Dispose the gadgets; FreeGadgets() ignores NULL arguments */

            FreeGadgets(glist);

            /* Free the VisualInfo */

            FreeVisualInfo(vi);
         }

         /* Release the DrawInfo structure */

         FreeScreenDrawInfo(win->WScreen,dri);
      }

      /* Say good-bye to the window... */

      CloseWindow(win);
   }

   /* ... and to the library */

   CloseLibrary(SelectGadgetBase);

   /* We did our job, now let's go home :-) */

   return (RETURN_OK);
}


