# Redriver ReExtended - notes for coding agents

## Rules

- Chased cars in Driver 1 missions (`d1chase.c`, `civCornerBrake` cars in `civ_ai.c`) never stand still on their own: they keep driving until the damage bar is full (100 %), and only then stop for good.
- One ram of the player fills at most a sixth of a chased car's damage bar (`D1ChaseCar_LimitHit`): at least 6 rams before the mission is done.
- A chased car always looks for a way through: it runs red lights, gives no way, overtakes and slips past slow cars, weaves away from the player close behind, turns off at junctions away from the player, and gets going again within 1 s when stuck: forward when its front sensors are free, backing up only when the way ahead is shut. It always prefers the road: off the road (pavement, grass) it heads for the nearest lane or the last road spot, lines up with the lane at low speed and only then speeds up again. Every change to the civilian AI, the mission code or the chase code must keep this.
- It drives fast but with sense: full speed on straights, slowing before bends (`CivCornerSpeed`), never full throttle into a corner; on sharp bends it slides round with the handbrake.
- Details and test method: `docs/dev-notes/d1-chase-paths.md`, traffic AI plan: `docs/dev-notes/smart-traffic-plan.md`.
