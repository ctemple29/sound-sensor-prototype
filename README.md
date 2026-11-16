# The Watcher
The Watcher
It sees: how close one is, measured in cm; how loud it is, measured 0-1023, it can know if touched, on or off
It calls you:  passerby, interested, disturbance, gone
It does:       red blinks with noise,  
For them:      passerby it shows its alive and watching; it tells you it can react and know continued presence. If you touch it, the red blinks rapidly, responding.
To them:       passerby: it informs of limited attention; interested: forcefully shows that it's still watching. 
It refuses:    to count your visits
Set aside:     Leo's Waiter: no stand by 11/18
Sits next to:  Hansel and Gretel, and pushes back
The tension:   Being too noisy allows the red to appear.
states: 
    { name: 'interested',   when: s => s.touch > 0.5,   hold: 0 },
    { name: 'disturbance', when: s => s.loud > 0.5,    hold: 0 },
    { name: 'passerby',    when: s => s.present > 0.5, hold: 0.5 },
    { name: 'gone',        when: s => s.present < 0.5, hold: 0.1 },
    { name: 'near',    when: s => s.present > 0.7, hold: 0.5 },
How to run: Plug Arudino Nano into power source.
Ensure wiring/soldering is proper
Let the sensors do their thing, OLED should display distance, red LED should flash when touched or glow when it loud.
Break Log: 11/5/26 - OLED failed to display proper distance - added averaging
11/13/26 - OLED continually overriden by 'interested' state - adjusted parameters
11/16/26 - OLED unclear - added 'near' state.
## Credits Cory Temple - All approved commits. Rocco Lombardo - Approver.  https://ctemple29.github.io/sound-sensor-prototype/


