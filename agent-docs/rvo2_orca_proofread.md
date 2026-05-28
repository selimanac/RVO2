### What it's all about

You may not be interested in all of those details, but it is kind of important for understanding the issues I'm going to describe below. I suffer a lot while reading and trying to understand academic papers (I don't understand half of them), and I don't want anyone else to feel the same :)

[ORCA](https://gamma.cs.unc.edu/ORCA/) is basically a "collision avoidance" algorithm. The word "collision" has nothing to do with our common understanding of collision, which we're used to in game development. It doesn't exactly check for collision or generate a manifold. Every agent looks forward to see the other agents' (neighbors') velocity, predicts their destination in time, and tries to avoid it by changing its own velocity. It assumes all agents are identical 50/50 partners in avoidance. This agent takes half of the required velocity correction, and the other agent is expected to take the other half when it computes its own velocity. ORCA operates in velocity space using geometric constraints. ORCA does not use forces, and it treats collision avoidance as a strict geometric constraint. This is the fundamental principle of ORCA, and this way it manages to achieve jitter-free (oscillatory-free) avoidance.

[RVO2](https://github.com/snape/RVO2) is just a small solution that uses the ORCA algorithm. It is mostly written as a proof of concept and bloated with academic stuff. It is a generic solution, and lots of features that may be required in game development are missing. "BUT" I'm shocked when I see that [Godot](https://docs.godotengine.org/en/4.0/tutorials/navigation/navigation_using_agent_avoidance.html) is using this library as-is. It is so poorly written; if I suggested this library to the Defold team to integrate, they wouldn't even bother to test it, they'd reject it instantly as-is :D So we can understand Godot vs Defold code quality from this.

### Collision Avoidance

There are a few known methods for generic collision avoidance; I'm not going into too much detail, I just want to share my experiments. Also, there could be lots of others, maybe even simpler ones, that I haven't even heard of. 

**Steering Behaviors:** They calculate a direction based on forces. In a dense crowd, it is almost inevitable to have the jittering issue. My previous example was based on Steering Behaviors, and I spent many hours trying to prevent this jittering using more and more stuff (I had to check collisions, raycast in 8 directions, etc., it became a mess). Also, as I mentioned before, it is hard to balance; if you change an agent's max speed, its range/radius, or the density of the crowd, the balance between Seek and Avoid shatters, requiring constant recalibration. But this is still an option where exact precision does not matter and clipping is acceptable.

**DetourCrowd:** Unity and Unreal use this one. It is a part of the [recastnavigation](https://github.com/recastnavigation/recastnavigation) solution. Kind of an industry standard for "navmesh" navigation. There is also a Defold Extension: https://github.com/abadonna/defold-detour. I didn't test the crowd collision avoidance performance of this one. It requires a navmesh to operate, so it's a bit out of my scope for this solution. 

**Continuum Crowds / Flow Fields:** I haven't tested this, nor do I have a lot of knowledge about it. It is basically a merge of Steering Behaviors and Flow Field tiles. I couldn't find examples related to this, and trying to build something from the ground up just by reading an academic paper is probably beyond my league. 

### Result

Since RVO2 is not a complete solution, it has many features that are missing or badly implemented.  
These are my findings, but every game has different needs; I mostly thought of it from the perspective of a basic RTS game. 


1. No goal/destination reached behavior

RVO2 has no built-in solution for reached behavior. This looks like a very easy thing to solve, but it can quickly becomes a pain in the ass. There are many ways to handle this behavior. In general, RTS game agents move in formation and have slots around the goal point. Using formations kind of simplifies things. 

2. No agent formations

You are not interested in this, but there is no built-in solution for formations.

3. No weight/responsibility Priority

This is one thing you are looking for. But actually, it goes against how ORCA works as-is. Adding different weights and responsibilities is exactly where the standard, out-of-the-box RVO2 library falls apart, because it assumes all agents are identical 50/50 partners in avoidance. An agent takes half of the required velocity correction, and the other agent is expected to take the other half when it computes its own velocity. That is ideal for symmetric crowd simulation, but it looks wrong for RTS units. An "idle" unit with a preferred velocity of zero can still move because ORCA is asking it to share collision-avoidance responsibility.

BUT there are ways to implement this. I already found the same author's [AVO2](https://github.com/snape/AVO2) solution. It's kind of like implementing a mass for the agents. I already have an idea which I may implement, but it is hard to say without actually doing and testing it. So it seems very highly possible to solve this.


4. Impact

This is something you are also looking for. I haven't thought about it much yet. As I said, there is no actual collision involved in this. But it should be possible by adding a large impact agent in the crowd—they will all move aside—OR for a single agent, it is possible to apply a high velocity in the opposite direction of the bullet hit (knockback), so all of them get this impact. I'm going to try these 2 ideas.

5. Agent Radius Performance

Basically, varying agent radii have a different impact on performance. If all agents have the same radius, it works very fast.

- Let's say we have agents with 30 units/pixels. All agents are the same.   
- It is obvious that there is no way more than 5-7 agents can be a neighbor of a single agent. So it has an expected performance.  

BUT. 

- If we have various sizes: many small 30 unit/pixel agents and many large 120 unit/pixel agents,  
- Large agents could have lots of small agent neighbors, like 15->20. This drops the performance significantly.   


6. Density

Density affects the performance. Especially if many agents targeting the same goal create a dense crowd. As I mentioned above, RTS games solve this by setting different slots around the goal position, so they are kind of separated. I haven't tested this scenario yet. 

### Performance

The most important part for me is that this is the most jitter-free and relatively easy-to-balance solution. There are always ways to improve performance, but this became a major issue in many of my previous experiences which I couldn't solve before. 

In general, crowd simulations are expensive operations and this is a very performant solution. But there are a few major bottlenecks as far as I can see (with RVO2 as-is, without developing it from scratch):

1. Neighbor Distance per agent

How far away (center-to-center) another agent must be to be considered a neighbor and included in the velocity computation. Agents beyond this distance are completely ignored. KD-tree query radius per agent: each step, each agent queries for all agents within X world units. Doubling the neighbor distance roughly doubles the tree query time per agent. The minimum safe value depends on agent density and max speed. Too low → missed collisions. 

2. Max Neighbors per agent

The dominant performance parameter. Too few neighbors risks unsafe avoidance in dense scenarios. Too many has an impact on performance. (Related to 5. Agent Radius Performance)

3. KD-tree

This solution uses a KD-tree internally to find the nearest neighbors of the agent. It is just another data structure like the Dynamic Tree used in Box2D and daabbcc. But it is a generic solution and it is not performing well. It requires a full rebuild on every iteration, which is very expensive. I have an alternative solution (using uniform grids and maybe spatial hashing backed by flat arrays) for this, but my solution may limit the use cases, so I have to think about it a little more. 

4. Multithread

It is possible to take advantage of multi-threading. The current implementation is not very cross-platform friendly. But I may consider implementing Defold's Job System, similar to what Godot did (they are using Godot's multi-threaded solution alongside this).


### Video

Here are some test videos. They are not the most fun things to watch, but I'm sharing them with you because you said you are practical, and you may see things that I don't, OR you can come up with different ideas/solutions, OR those might give you new inspirations about what you can do about all of this. 

Don't bother checking the FPS on those examples. Raylib is using most of the frame time for drawing. The most important metric is the SIM Time. Those examples were just developed in a limited time, and I didn't pay attention to details.

You should consider those numbers are from a C/C++ implementation and from my dev environment. There is no Lua overhead and no other logic (like collision detection, etc.). There will be many more things involved in a real game. 

---

Agents are trying to reach the goal position, which is on the opposite side of the circle for every agent. Some agents are jittering when they reach the goal because of my bad arrival implementation; please don't mind it.   
  
 1500 agents, sim time ~1.15ms   
https://www.youtube.com/watch?v=82RXFmJa6G4

---

Single-threaded "Follow the Leader". 
This is a very dense and very stressful test for this solution.   
Using the same radius and Max Neighbors per agent.  
  
Note: I'm turning on/off the following. 
4300 agents, sim time ~4ms   
  
https://www.youtube.com/watch?v=eG8HzSFnpow

---

Single-threaded "Follow the Leader"   
This is a very dense and very stressful test for this solution.   
Using different radii and Max Neighbors per agent.  
  
1100 agents, sim time ~2ms   
3000 agents, sim time ~6ms   
    
https://www.youtube.com/watch?v=SzYc5672MjQ

---

Multi-threaded "Follow the Leader"   
Uses a 10-core CPU, but it is not ideal. In general use cases, it should be tested on a max of 4 cores.  
This is a very dense and very stressful test for this solution.   
Using different radii and Max Neighbors per agent.  
  
4100 agents, sim time ~1.9ms   
  
https://www.youtube.com/watch?v=OyutcHkDclk

---

RTS-style select and move to the same goal.  
Single-threaded   
Using different radii and Max Neighbors per agent.  
This is a very dense and very stressful test for this solution.   
  
1400 agents, sim time ~2ms   
  
https://www.youtube.com/watch?v=iEBHf45lDXU

---

RTS-style select and move to the same goal   
Multi-threaded. 
Using different radii and Max Neighbors per agent.  
This is a very dense and very stressful test for this solution.   
  
4500 agents, sim time ~2ms   
  
https://www.youtube.com/watch?v=dfYmGQOyOHQ

---

Showcase for static obstacle avoidance. Agents try to reach their corners. Obstacles are defined as vertices.   
  
I haven't stressed those out yet. Just very basic, and fast.  

https://www.youtube.com/shorts/04LJGipxzTw

---

Showcase for static obstacle avoidance using paths. Agents try to reach their corners but using paths.   

I haven't stressed those out yet. Just very basic, and fast.  

https://www.youtube.com/shorts/htfByOiBEEg

