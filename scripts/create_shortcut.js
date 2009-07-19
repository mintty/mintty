ws = WScript;
sh = ws.CreateObject("WScript.Shell");

cygkey15 = "Cygnus Solutions\\Cygwin\\mounts v2\\/\\native"
cygkey17 = "Cygwin\\setup\\rootdir";
softkey = "HKLM\\Software\\";
softkey64 = softkey + "WoW6432Node\\";

try { cygbase = sh.RegRead(softkey + cygkey17); }
catch (e) {
  try { cygbase = sh.RegRead(softkey64 + cygkey17); }
  catch (e) {
    try { cygbase = sh.RegRead(softkey + cygkey15); }
    catch (e) {
      try { cygbase = sh.RegRead(softkey64 + cygkey15); }
      catch (e) {
        ws.Echo("Error: Could not find Cygwin registry key.");
        ws.Quit(1); 
} } } }

cwd = sh.CurrentDirectory;
link = sh.CreateShortcut("MinTTY.lnk");
link.TargetPath = cwd + "\\mintty.exe";
link.Arguments = "-";
link.WorkingDirectory = cygbase + "\\bin";
link.IconLocation = cwd + "\\mintty.exe,0";
link.Description = "Cygwin Terminal";

try { link.Save(); }
catch (e) {
  ws.Echo("Error: Could not write shortcut to disk.");
  ws.Quit(1);
}
