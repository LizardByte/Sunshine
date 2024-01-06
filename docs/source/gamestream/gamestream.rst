GameStream
==========
Nvidia announced that their GameStream service for Nvidia Games clients will be discontinued in February 2023.
Luckily, Sunshine performance is now on par with Nvidia GameStream. Many users have even reported that Sunshine
outperforms GameStream, so rest assured that Sunshine will be equally performant moving forward.

Migration
---------
We have developed a simple migration tool to help you migrate your GameStream games and apps to Sunshine automatically.
Please check out our `GSMS <https://github.com/LizardByte/GSMS>`__ project if you're interested in an automated
migration option. GSMS offers the ability to migrate your custom and auto-detected games and apps. The
working directory, command, and image are all set in Sunshine's ``apps.json`` file. The box-art image is also copied
to a specified directory.

Internet Streaming
------------------
If you are using the Moonlight Internet Hosting Tool, you can remove it from your system when you migrate to Sunshine.
To stream over the Internet with Sunshine and a UPnP-capable router, enable the UPnP option in the Sunshine Web UI.

.. note:: Running Sunshine together with versions of the Moonlight Internet Hosting Tool prior to v5.6 will cause UPnP
   port forwarding to become unreliable. Either uninstall the tool entirely or update it to v5.6 or later.

Limitations
-----------
Sunshine does have some limitations, as compared to Nvidia GameStream.

- Automatic game/application list.
- Changing game settings automatically, to optimize streaming.
