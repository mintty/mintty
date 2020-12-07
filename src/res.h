// res.h
// common definitions for res.rc resource file and windialog.c

// The height of the dialog box used for the Options menu used to be 
// configured in res.rc. Some values in windialog.c are derived from it though.
// This would not be a problem if we could extract the configured value from the 
// resource. How this can be done, however, is a well-hidden secret in this 
// mysterious Windows resource world. We could also try to derive the value 
// during initialisation as tentatively demonstrated in windialog.c but the 
// value is already needed before it would be available that way - totally insane!
// Therefore, its setting is taken out to this common include file.

#define DIALOG_HEIGHT 201
                   // +11 per ctrl_columns row
                   // +20 per titled single-line set of radio buttons
                   // +31 per titled double-line set of radio buttons

#define DIALOG_WIDTH 256
//#define DIALOG_WIDTH 310  // for CJK localisation

#define DIALOG_TITLE "Options"
#define DIALOG_CLASS "ConfigBox"

#define DIALOG_FONT "MS Shell Dlg"
#define DIALOG_FONTSIZE 8
//#define DIALOG_FONT "Tahoma"

//#define DIALOG_FONT "Calibri"
//#define DIALOG_FONT "Trebuchet MS"
//#define DIALOG_FONT "Lucida Sans Unicode"

