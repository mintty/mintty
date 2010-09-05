ws = WScript;
sh = ws.CreateObject("WScript.Shell");

cygkey = "Cygnus Solutions\\Cygwin\\mounts v2\\/\\native"
softkey = "HKLM\\Software\\";
softkey64 = softkey + "WoW6432Node\\";

try { cygbase = sh.RegRead(softkey + cygkey); }
catch (e) {
  try { cygbase = sh.RegRead(softkey64 + cygkey); }
  catch (e) {
    ws.Echo("Error: Could not find Cygwin 1.7 registry key.");
    ws.Quit(1); 
  }
}

cwd = sh.CurrentDirectory;
link = sh.CreateShortcut("mintty.lnk");
link.TargetPath = cwd + "\\mintty.exe";
link.Arguments = "-";
link.WorkingDirectory = cygbase + "\\bin";
link.IconLocation = cwd + "\\mintty.exe,0";

try { link.Save(); }
catch (e) {
  ws.Echo("Error: Could not write shortcut to disk.");
  ws.Quit(1);
}
