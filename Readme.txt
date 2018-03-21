Overview:
This is the Hygge power repo for esp-32 code. It is built using the mongoose-os framework, which rides on top of the esp32 SDK from expressIf.  hygge-esp32 consists of this repo and a git subtree module (hygge-esp32-http-server). This subtree module was taken from the mongoose-os code base and adapted for use by Hygge. Most notably, it now contains http endpoints which serve a form for a user to configure WiFi and apply these WiFi settings.  

Accessing Mongoose Host Wifi:
plug the demo board into USB to provide power to it.  You should see a WiFi access point come up with the name "Mongoose_<random string>".  Connect to this access point and use password "Mongoose".  After connecting, browse to: http://hygge (default is http://192.168.4.1) and enter your home wifi access point name and password. After clicking 'submit' your device will save the credentials and reboot and attempt to connect to your home wifi.  
Setting this configuration is accompished in hygge-esp32-http-server repo.  It has a static server which serves the files on the file system of the ESP-32. These html files are stored in /fs directory of hygge-esp32 repo.  If you would like to allow configuration of other wifi parameters such as default gateway, set a static ip, netmask, etc: from the root of hygge-esp32, look at the file deps/wifi/mos.yml (note that you must build in order for the deps repository to be pulled down- alternatively you can clone the wifi repo in the mongoose-os project) which contains configuration options for setting all wifi related parameters.  Look in the http-server repo for how we set wifi.sta.ssid for an example of how to set parameters programatically. 

AWS configuration:
The device is currently configured to connect to nickamuch's aws instance through the parameters set in mos.yml. Full instructions for how to set up the connection are given here: https://mongoose-os.com/docs/cloud_integrations/aws.html
In short, you need to install the mos tool and install the AWS Command Line Interface tool, both of which are described in detail above.  After installing these tools, you can connect your device via usb and tun the command "mos aws-iot-setup" which will not only provision the device with your AWS connection info and credentials but also create a 'thing' in your AWS account which corresponds to this device.

To send messages to your device or listen to messages from the device, log onto your AWS console and click 'AWS IoT'.  
If your device is configured/added to AWS, you should be able to click 'manage' on the left side console and see your device under 'Things'.
If you can see your devie, click 'Test' in the left hand console. Set Quality of Service to '1', subscribe to the topic you'd like to listen/post to, and either wait for messages to come in or send a message.

When developing, the esp32 forgets your wifi credentials after every reflash.  You can set them permanently by uncommenting and updating the fields in mos.yml

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

Testing:
As of 3/20/2018 Nick has only tested manually by logging onto the aws iot console, subscribing to topics and posting to topics.  
For example: 
Subscribe to: 001/status/device_info
Publish to: 001/cmd/send_status
	with message: {version: 1, id: 2, status_type: 4}
And see that the device recieves your message and sends back a device info status.

Moving forward, I would suggest that a test script be written using the AWS SDK for python to test the endpoints programatically. 
