GameStream
==========
Nvidia announced that their GameStream service for Nvidia Games clients will be discontinued in February 2023.
Luckily, Sunshine performance is now on par with Nvidia GameStream. Many users have even reported that Sunshine
outperforms GameStream, so rest assured that Sunshine will be equally performant moving forward.

Migration
---------
We have developed a simple migration tool to help you migrate your GameStream games and apps to Sunshine automatically.
Please check out our `GSMS <https://github.com/LizardByte/GSMS>`_ project if you're interested in an automated
migration option. At the time of writing this GSMS offers the ability to migrate your custom games and apps. The
working directory, command, and image are all set in Sunshine's ``apps.json`` file. The box-art image is also copied
to a specified directory.

Limitations
-----------
Sunshine does have some limitations, as compared to Nvidia GameStream.

- Automatic game/application list.
- Changing game settings automatically, to optimize streaming.
