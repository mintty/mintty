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
#define DIALOG_HEIGHT 182

#define DIALOG_TITLE "Options"
#define DIALOG_CLASS "ConfigBox"

#define DIALOG_FONT "MS Shell Dlg"
//#define DIALOG_FONT "Tahoma"

//#define DIALOG_FONT "Calibri"
//#define DIALOG_FONT "Trebuchet MS"
//#define DIALOG_FONT "Lucida Sans Unicode"

