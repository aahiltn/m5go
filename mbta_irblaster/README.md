  # MBTA Tracker & Lightbar Control

A little side project. 

### Lightbar Control
Bought a couple of really cheap lightbars from Five Below that come with these pretty archaic remotes, so I was trying to think of ways I could use bring them to the 21st century. 

They seemed like pretty basic IR receivers, and I had the M5GO by M5Stack just lying around as an old hackathon prize. I figured that had an IR blaster, and by connecting the Arduino to WiFi, I'd be able to use it as a proper IoT device. 

After doing some research, there's a pretty handy service *Sinric Pro*. It connects to Google Home and Amazon Alexa, so you can make an account, and for free, get some credentials to link an Arduino. I set it as a Smart Dimmable Light, got the credentials, and cool!

Using the IR Blaster/Receiver, I was able to get the IR codes for various settings of the lightbar, hardcoded it in the code. and boom! That worked. After connecting it to Google Home, was able to connect it. 

(I haven't tried it yet on university wifi, but note that you'll need to note down the MAC address of the Arduino for registration.)

### MBTA Tracker
It's inspired by this [video](https://www.youtube.com/watch?v=jHDNbvv6Rjo) by Matt Ognibene, which was really well done, but to be honest, it's something I've been thinking of for a while.

Since I had this arduino running now, might as well give it more use than being a glorified remote.

The MBTA has an API at https://api-v3.mbta.com/. I didn't bother with getting an API key, seemed to work fine without it, but keep this in mind to be nice. 

I'll be commuting to downtown Boston soon, so I wanted a way to keep track of inbound trains at a quick glance. The other options right now are entering the locations on Google Maps or the Transit app (which is something I did for a while), or the [ProximiT](https://apps.apple.com/us/app/proximit-mbta-boston-transit/id721033937) app, which is a pretty good piece of software. I haven't used MBTA Go yet, maybe that's really good, idk. 

Not to dox myself, but living close to Northeastern's campus, Ruggles and Northeastern University are the closest stops for me: thus, the Orange and Green-E trains. Since I only need the inbound directions, that cuts down on results. I would have included Commuter Rail too, but it's a small screen.

I started off with one request of 5 trains every 30 seconds, which, when testing, kept only returning Orange Line results and no Green Line. After debugging, I realized my filters weren't bad; rather, the Green Line was currently stopped, so the next 5 inbound trains were all Orange Line. Wild. 

To avoid situations like this, I split it into two separate calls, one Green Line and one Orange Line. Not too nice to the building WiFi or the MBTA servers, so I reduced it to one call every two minutes, assuming this is a doodad that's gonna be constantly running. 

Then, I wanted to factor in the time taken to reach the station. For me, this is like ~5-15 minutes, so in order to add the countdown, I needed to sync with a clock. ntp.org is the standard. Thus, if a train is arriving within this time range, it wouldn't show it as a possible option. (I didn't really see the point of the "Last Chance" feature in Matt's video, but hey, cool feature ig.)

Then, blending in the IR blaster code from before, it works!



