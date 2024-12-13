#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <Application.h>
#include <String.h>
#include <Resources.h>
#include <Roster.h>
#include <Mime.h>
#include <Path.h>
#include <AppFileInfo.h>

class VLCLauncherApp : public BApplication {
	public:
		VLCLauncherApp(const char *signature, int argc, char **argv);
		~VLCLauncherApp() {};
		void			RefsReceived(BMessage *pmsg);
		virtual void 	ReadyToRun();
		BString			GetVLCPath(void);
	private:
		BMessenger  	fTrackerMessenger;
};

VLCLauncherApp::VLCLauncherApp(const char *signature, int argc, char **argv)
	: BApplication(signature)
{
}

BString
VLCLauncherApp::GetVLCPath(void)
{
	return "VLC";
}

void
VLCLauncherApp::RefsReceived(BMessage *pmsg)
{
	if (pmsg->HasMessenger("TrackerViewToken")) {
		pmsg->FindMessenger("TrackerViewToken", &fTrackerMessenger);
	}

	uint32 type;
	int32 count;
	status_t ret = pmsg->GetInfo("refs", &type, &count);
	if (ret != B_OK || type != B_REF_TYPE)
		return;

	BString commandLine = GetVLCPath();

	entry_ref ref;	
	for (int32 i = 0; i < count; i++) {
   		if (pmsg->FindRef("refs", i, &ref) == B_OK)
   		 {
   		    BPath file=BPath(&ref);
   		    commandLine += " \"";
   		    commandLine += file.Path();
   		    commandLine += "\"";
   		 }
   	}
   	commandLine += " &";
   	system(commandLine.String());
   	Quit();
}

void
VLCLauncherApp::ReadyToRun()
{
	BString commandLine = GetVLCPath();
	commandLine += " &";
    system(commandLine.String());
    Quit();
}


int main(int argc, char **argv)
{
	VLCLauncherApp application("application/x-vnd.vlc-launcher", argc, argv);
	application.Run();
	return 0;
}


