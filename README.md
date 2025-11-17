# OVRLipSync-UE5
Oculus Lip Sync Plugin precompiled for Unreal 5.

https://youtu.be/clPu9RlIGuw?si=9tVsqKeNiNS2VElS


Addtional Code for runtime audio generation (hex -> sound wave) & LipSync Sequence generation.
<img width="2147" height="254" alt="image" src="https://github.com/user-attachments/assets/437557fa-7e5c-40dc-b353-29ff4b8e6368" />



As you might know, the plugin available from the link in the offical docs don't work in Unreal Engine 5. I fixed it with the help of a lot of internet strangers and compiled the plugin. Tested on Quest2/3 and Quest Pro.
## How to use:

1. Copy the <mark>OVRLipSync</mark> folder in this repository to the Plugins folder in your Unreal Engine 5 project. From the **Edit** menu, select **Plugins** and then **Audio**. You should see the **Oculus Lipsync** plugin as one of the options. Select Enabled to enable the plugin for your project.

2. Add the following to your **DefaultEngine.ini**:
```ini
[Voice]
bEnabled=true
``` 

3. Check the official [Docs](https://developer.oculus.com/documentation/unreal/audio-ovrlipsync-unreal/)


## License
This plugin is the property of [Meta](https://about.meta.com/) and is provided under the Oculus SDK License, which allows for personal and commercial use of the plugin. By using this plugin, you agree to the terms of the Oculus SDK License.
