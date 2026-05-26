- No goal/destination reached behavior
- No aget formations
- No weight/responsibility  Priority

 strip out the academic bloat 
https://docs.godotengine.org/en/4.0/tutorials/navigation/navigation_using_agent_avoidance.html

Adding different weights and sizes is exactly where the standard, out-of-the-box RVO2 library falls apart, because it assumes all agents are identical 50/50 partners in avoidance.


this agent takes half of the required velocity correction
the other agent is expected to take the other half when it computes its own velocity

That is ideal for symmetric crowd simulation, but it looks wrong for RTS units. An idle unit with preferred velocity zero can still move because ORCA is asking it to share collision-avoidance responsibility.

neighborDist
How far away (center-to-center) another agent must be to be considered a neighbor and included in velocity computation. Agents beyond this distance are completely ignored.

timeHorizon
How many seconds ahead the ORCA algorithm plans agent-to-agent avoidance. Larger values make agents start avoiding each other earlier/.

maxNeighbors  — dominant performance parameter

Caps how many neighbors enter the ORCA linear program (linearProgram2).
LP is O(k²): each ORCA constraint is tested against all others.
agentNeighbors_ insertion maintains sorted order → O(k) per insert.


// Use Uniform Grids and Spatial Hashing backed by flat arrays.
circle
https://www.youtube.com/watch?v=82RXFmJa6G4

follow openmp
https://www.youtube.com/watch?v=OyutcHkDclk

follow
https://www.youtube.com/watch?v=SzYc5672MjQ

move openmp
https://www.youtube.com/watch?v=dfYmGQOyOHQ

move
https://www.youtube.com/watch?v=iEBHf45lDXU

block
https://www.youtube.com/shorts/04LJGipxzTw

roadmap
youtube.com/shorts/htfByOiBEEg
