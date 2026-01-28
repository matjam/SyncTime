

#ifndef CLIB_SELECTGADGET_PROTOS_H
#define CLIB_SELECTGADGET_PROTOS_H


/* Prototypes for the "select gadget" BOOPSI public class functions */


Class *ObtainSelectGClass(void);

ULONG InitSelectGadgetA(struct Gadget *gadget, ULONG flags, struct TagItem *taglist);

ULONG InitSelectGadget(struct Gadget *gadget, ULONG flags, Tag tag1, ...);

void ClearSelectGadget(struct Gadget *gadget);

ULONG SetSGAttrsA(struct Gadget *gadget, struct Window *win, struct Requester *req, ULONG flags, struct TagItem *taglist);

ULONG SetSGAttrs(struct Gadget *gadget, struct Window *win, struct Requester *req, ULONG flags, Tag tag1, ...);

ULONG GetSGAttr(ULONG attributeid, struct Gadget *gadget, ULONG *storage);


#endif


