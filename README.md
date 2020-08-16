# dl.netreceive~

dl.netsend~/dl.netreceive~ are Max objects, currently under development, that can be used to send uncompressed audio over the internet between Max/MSP patches behind two different NAT routers. 

The goal of this particular project is to prioritize low latency and dynamic channel routing. In order to accomplish this, the Libuv library is used to accomplish asynchronous event handling of the incoming data-grams. The UDP transfer protocol is used to both minimize latency and overcome the need for sample clock synchronization. 

In its current state, it exists as a “proof of concept” showing that Libuv can run in tandem with the Max API and a call for development support. Functional modifications and community feature requests are listed at the end of this document. 

////////////////////////////////

Download / Build

dl.netsend~ and dl.netreceive~ are two different externals that work in tandem. They are in two separate repositories on my Github (https://github.com/dlandon86?tab=repositories ). 
The easiest way to build/test the externals is to download the Max SDK and follow the API documentation found here: https://cycling74.com/downloads/sdk 
Both externals utilize Libuv and Pthreads. 
Libuv is open source and can be downloaded here: https://github.com/libuv/libuv . Installation and build instructions can be found on their Read Me. Documentation for Libuv can be found here: https://github.com/thlorenz/libuv-dox  and here: http://docs.libuv.org/en/v1.x/index.html#documentation 
If you’re on Windows, POSIX Threads for Win32 can be found here: https://sourceware.org/pthreads-win32/ 

////////////////////////////////

Concept:
Each instance of dl.netsend~ is passed arguments for (int) number of channels, (sym) ip address, and (sym) port of the intended recipient. Each instance of dl.netreceive~ is passed (int) number of channels and (sym) port to listen on. In this way, each instance of a dl.netsend~ represents a single individual that can receive any number of channels from you, and each dl.netreceive~ represents a single sender of any number of channels that you can receive. This breaks up the broadcast/mixdown approach of many networked audio solutions and frees you up to send and receive audio in a dynamic way. In a group of 5 performers, Person 1 could send audio to persons 3 and 5 for manipulation and person 3 could send to person 4 who could send back to 1. Person 4 could send to person 2 who sends back to person 5, etc. In this way, each performer could be thought of as a module in a “global” modular synthesizer! 

////////////////////////////////

Community sourced feature requests and other implementations that could be looked into:
- hub and spoke network implementation (currently it is easiest to use other software to set up a hub and spoke VPN and then use those ip addresses. 
- Use cm / aoo (https://git.iem.at/cm/aoo?fbclid=IwAR2hlgTvUNpbmsH2aMJC8vn6IxtrAg-wBirpwLrm6N0jEnfJNmNio_NggIk) and then native Max externals to transmit the signal via UDP.
- wrap the NetJack2 client/server in a Max external
- Use Jacktrip and have Max control the making/breaking of Jack connections by calling them from the shell.

////////////////////////////////

Necessary Functional Implementations for the External:
- improved memory allocation and freeing of memory for Libuv’s loop pointer and buffer.base
- In dl.netsend~, Reliable way of closing the loop and thus the pthread it is running on. 
- Reliable network communication when switching between Ipv4 and Ipv6
- Meta data for multichannel setups

If interested in collaborating on the project and have questions, feel free to contact me via email at david.landon@colorado.edu
