This is the Hygge power repo for esp-32 code. It is built using the mongoose-os framework, which rides on top of the esp32 SDK from expressIf.  The hygge-esp32 consists of this repo and a git subtree module (hygge-esp32-http-server). This subtree module was taken from the mongoose-os code base and adapted for use by Hygge. Most notably, it now contains http endpoints which serve a form for a user to configure WiFi and apply these WiFi settings.  

To update the http-server module (which is a git subtree in the hygge-esp32 module):
1) git clone the hygge-esp32-http-server repo
2) make changes and commit these changes.
3) git clone the hygge-esp32 repo
4) cd hygge-esp32
5) git subtree add --prefix deps/http-server https://github.com/nickamuch/hygge-esp32-http-server.git master --squash
6) git push -u origin master   # update the remote repo with the new subtree

Environment and setup: 
Documentation is located at: https://mongoose-os.com/docs/quickstart/setup.html
The following instructions assume you've installed the mos tool, which is also described at the page above.


To build over the network:
mos build --platform esp32

To build locally, you need to install docker and then run the local build command, which will pull down a docker image from github (only the first time) if docker is running correctly and then build the firmware using that docker image.  More information on the build process and yaml (.yml files) is here: https://mongoose-os.com/docs/book/build.html
The local build command is: mos build --platform esp32 --local 

Flashing: You can flash the device with the command:
mos flash

Viewing logs: 
mos console


