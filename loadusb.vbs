Set WshShell = Wscript.CreateObject("Wscript.Shell")
Set FSO = CreateObject("Scripting.FileSystemObject")

TargetFolder = WshShell.CurrentDirectory
Set Folder = FSO.GetFolder(TargetFolder)
parent = Folder.Name

WshShell.Run "avrdude -q -patmega328p -carduino -P\\.\COM3 -b57600 -D -Uflash:w:Release\" & parent & ".hex:i"