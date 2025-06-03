import pyliblo3
import sys

vmap = [None] * 256

#set custom values
vmap[ord('0')] = 0
vmap[ord('A')] = 50
vmap[ord('B')] = 100
vmap[ord('C')] = 150
vmap[ord('D')] = 200
vmap[ord('E')] = 250
vmap[ord('F')] = 255

# Convertstring description to the channel list for /frame command
# Symbols other than listed above are ignored
def str2chans(s, chan_shift=0):
    chans = []
    for j in range(len(s)):
        tmp = vmap[ord(s[j])]
        if not tmp is None:
            chans.append(j + chan_shift)
            chans.append(tmp)
    return chans

#s = "A BB CD"
#print(str2chans(s));

class DmxReq(pyliblo3.Bundle):
    dmx_url = ""
    def __init__(self):
        super().__init__()

    def addFrame(self, univ, vstr, chan_shift=0):
        self.add("/frame", int(univ), *str2chans(vstr))

    def send(self, start_time, fade_time):
        self.add("/mtc_time", start_time)
        self.add("/fade_time", float(fade_time))
        pyliblo3.send(DmxReq.dmx_url, self)

DmxReq.dmx_url = "osc.udp://localhost:8000"

# Port can be set with argv[1]
if 1 < len(sys.argv):
    DmxReq.dmx_url = f"osc.udp://localhost:{sys.argv[1]}"

req = DmxReq()
req.addFrame(1, "FFFFFF")
req.send("+0:02", 5.0)

req = DmxReq()
req.addFrame(1, "    AAAAAA")
req.send("+0:04", 5)

# Initial fading, will be played first
req = DmxReq()
req.addFrame(1, "0"*200) # fade out everything
req.send("now", 0.5)

