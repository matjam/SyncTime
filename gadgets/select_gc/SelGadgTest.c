

/*  Select.gadget test (15.1.2001)  */
/*  Written for SAS/C               */
/*  Compile: SC LINK SelGadgTest    */
/*  © 2001 Massimo Tantignone       */


#include "exec/types.h"
#include "exec/libraries.h"
#include "dos/dos.h"
#include "intuition/intuition.h"
#include "intuition/gadgetclass.h"
#include "libraries/gadtools.h"
#include "proto/intuition.h"
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
   ULONG class, code, done = FALSE;
   ULONG width = 640, height = 200;
   struct Gadget *gad1, *gad2, *gad3, *gad4, *gad5;
   struct Gadget *iaddress;
   UBYTE msgbuffer[40], versbuffer[40];
   STRPTR labels1[] = { "First option",
                        "Second option",
                        "Third option",
                        "Fourth option",
                        NULL };
   STRPTR labels2[] = { "This is an",
                        "example of",
                        "my BOOPSI",
                        "pop-up",
                        "gadget class.",
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
                               "SelGadgTest","%s"," Ok " };

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
                                 WA_Title,"select.gadget test",
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
         /* A standard pop-up gadget, with some attributes overridden */

         gad1 = NewObject(NULL,"selectgclass",GA_Left,40,
                                              GA_Top,32 + win->BorderTop,
                                              GA_RelVerify,TRUE,
                                              GA_DrawInfo,dri,
                                              GA_Text,"With _delay",
                                              GA_ID,1,
                                              SGA_Underscore,'_',
                                              SGA_TextPlace,PLACETEXT_ABOVE,
                                              SGA_Labels,labels1,
                                              SGA_Separator,FALSE,
                                              SGA_PopUpDelay,400,
                                              SGA_ItemSpacing,2,
                                              SGA_FollowMode,SGFM_FULL,
                                              SGA_MinTime,200,
                                              SGA_MaxTime,200,
                                              SGA_PanelMode,SGPM_DIRECT_NB,
                                              TAG_END);

         /* A "quiet" pop-up gadget, which could be attached to another one */

         gad2 = NewObject(NULL,"selectgclass",GA_Previous,gad1,
                                              GA_Top,72 + win->BorderTop,
                                              GA_RelVerify,TRUE,
                                              GA_DrawInfo,dri,
                                              GA_Text,"Quie_t",
                                              GA_ID,2,
                                              SGA_Underscore,'_',
                                              SGA_Labels,labels2,
                                              SGA_PopUpPos,SGPOS_RIGHT,
                                              SGA_Quiet,TRUE,
                                              SGA_Separator,FALSE,
                                              SGA_ReportAll,TRUE,
                                              SGA_BorderSize,8,
                                              SGA_FullPopUp,TRUE,
                                              SGA_PopUpDelay,1,
                                              SGA_DropShadow,TRUE,
                                              SGA_ListJustify,SGJ_LEFT,
                                              TAG_END);

         /* Let's make it perfectly square, and place it correctly */

         if (gad1 && gad2)
         {
            SetAttrs(gad2,
                     GA_Left,gad1->LeftEdge + gad1->Width - gad2->Height,
                     GA_Width,gad2->Height,
                     TAG_END);
         }

         /* A "sticky" list-type pop-up gadget */

         gad3 = NewObject(NULL,"selectgclass",GA_Previous,gad2,
                                              GA_Top,32 + win->BorderTop,
                                              GA_RelVerify,TRUE,
                                              GA_DrawInfo,dri,
                                              GA_Text,"Sticky b_utton",
                                              GA_ID,3,
                                              SGA_Underscore,'_',
                                              SGA_Labels,labels1,
                                              SGA_Active,3,
                                              SGA_ItemSpacing,4,
                                              SGA_SymbolOnly,TRUE,
                                              SGA_SymbolWidth,-21,
                                              SGA_Sticky,TRUE,
                                              SGA_PopUpPos,SGPOS_BELOW,
                                              SGA_BorderSize,4,
                                              SGA_PopUpDelay,1,
                                              SGA_Transparent,TRUE,
                                              TAG_END);

         /* Let's place it correctly */

         if (gad3)
         {
            SetAttrs(gad3,GA_Left,win->Width - gad3->Width - 40,TAG_END);
         }

         /* A pop-up gadget which simply reflects the global user settings */

         gad4 = NewObject(NULL,"selectgclass",GA_Previous,gad3,
                                              GA_Top,72 + win->BorderTop,
                                              GA_RelVerify,TRUE,
                                              GA_DrawInfo,dri,
                                              GA_Text,"S_imple",
                                              GA_ID,4,
                                              SGA_Underscore,'_',
                                              SGA_Labels,labels1,
                                              TAG_END);

         /* Let's place it correctly */

         if (gad4)
         {
            SetAttrs(gad4,GA_Left,win->Width - gad4->Width - 40,TAG_END);
         }

         /* A read-only gadget to display text - yes, we can do this too! */

         /* Note: the GA_ReadOnly tag was first introduced in */
         /* the V44 include files; its value is 0x80030029.   */

         gad5 = NewObject(NULL,"selectgclass",GA_Previous,gad4,
                                              GA_Left,win->BorderLeft + 1,
                                              GA_DrawInfo,dri,
                                              GA_Text,versbuffer,
                                              GA_ID,5,
                                              GA_ReadOnly,TRUE,
                                              TAG_END);

         /* Let's place it correctly, with some relativity */

         if (gad5)
         {
            SetAttrs(gad5,
                     GA_RelBottom,-(gad5->Height + win->BorderBottom),
                     GA_RelWidth,-(win->BorderLeft + win->BorderRight + 2),
                     TAG_END);
         }

         /* If all went ok, add the gadgets to the window and display them */

         if (gad1 && gad2 && gad3 && gad4 && gad5)
         {
            AddGList(win,gad1,-1,-1,NULL);
            RefreshGList(gad1,win,NULL,-1);
         }

         /* Now let's handle the events until the window gets closed */

         while (!done)
         {
            Wait(1 << win->UserPort->mp_SigBit);

            while (imsg = (struct IntuiMessage *)GetMsg(win->UserPort))
            {
               class = imsg->Class;
               code = imsg->Code;
               iaddress = imsg->IAddress;
               ReplyMsg((struct Message *)imsg);

               if (class == IDCMP_CLOSEWINDOW) done = TRUE;

               else if (class == IDCMP_GADGETUP)
               {
                  sprintf(msgbuffer,"Gadget: %ld, Item: %ld",
                                    iaddress->GadgetID,
                                    code);

                  SetGadgetAttrs(gad5,win,NULL,GA_Text,msgbuffer,TAG_END);
               }

               else if (class == IDCMP_VANILLAKEY)
               {
                  switch (toupper(code))
                  {
                     case 'D': iaddress = gad1; break;
                     case 'T': iaddress = gad2; break;
                     case 'U': iaddress = gad3; break;
                     case 'I': iaddress = gad4; break;
                     default: iaddress = NULL;
                  }

                  if (iaddress)
                  {
                     ActivateGadget(iaddress,win,NULL);
                     sprintf(msgbuffer,"Key: %lc",code);
                     SetGadgetAttrs(gad5,win,NULL,GA_Text,msgbuffer,TAG_END);
                  }
               }

               else if ((class == IDCMP_MOUSEBUTTONS) &&
                        (code == SELECTDOWN))
               {
                  SetGadgetAttrs(gad5,win,NULL,GA_Text,versbuffer,TAG_END);
               }

               else if (class == IDCMP_REFRESHWINDOW)
               {
                  BeginRefresh(win);
                  EndRefresh(win,TRUE);
               }
            }
         }

         /* If the gadgets were added, remove them */

         if (gad1 && gad2 && gad3 && gad4 && gad5)
         {
            RemoveGList(win,gad1,5);
         }

         /* Dispose the gadgets; DisposeObject() ignores NULL arguments */

         DisposeObject(gad1);
         DisposeObject(gad2);
         DisposeObject(gad3);
         DisposeObject(gad4);
         DisposeObject(gad5);

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


