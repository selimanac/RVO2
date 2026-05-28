> I wonder what balance we should strike between "Alex is asking for this very specific thing" and "how can we make this an extension that is usable across many projects for many people". 

Actually, all the things I mentioned were for the general use case; they weren't for Alex's specific thing.

Let me wrap up quickly:

* **Code Quality:** This code is not terribly bad; as I said, Godot already uses it as it is. But I know I can do better. I did it for Graph Pathfinder, and currently, I'm working on a [FABRIK IK](https://x.com/selimanac/status/2054463689504256463) solution which will eventually end up on Defold. So I prefer to write this from scratch, even if my aim is just for the community.

* **KD-tree:** The KD-tree is good for a generic use case. It handles arbitrary world boundaries out of the box, but it has known performance issues. I’ll probably spend most of my time replacing it with flat arrays. This isn’t something specific to you. I haven’t decided to make the change yet.

* **Goal/destination reached behavior:** I may pass on this for the initial/first release. Alternatively, I can basically call a Lua callback to inform the developer by saying, "Do what you want to do". But this is just a quick idea; I have to think about it when the time comes.

* **No agent formations:** I'm definitely going to pass on this for the first release. Maybe in the future.

* **No weight/responsibility Priority:** This is not just something you asked for, it is a must-have feature.

* **Multithread:** I want this, but I have never used Defold's JobSystem before. Maybe I can pass on it for the first release, but I'm definitely going to give this a higher priority later on.


> Perhaps both are achievable, where I can commission a custom version of the extension or something.
> By the way, we should probably discuss compensation? Once you get an idea of the scope and I finally decide I actually want to make this exact game.

Maybe we can consider a custom version, but it might not be needed. It's too early to say.

> Where option 1 is an implementation where radii are all identical and option 2 is an implementation where radii can vary BUT in actual usage all agents have identical radii: Is option 2 more expensive than option 1?

I don't think I understand option 2. It is possible to use different radii now? 
If the radii are all identical, then it will be more performant. Small differences are generally not an issue, like 20 to 25, etc.

> Where I'm going with this: Mostly I would keep all agent radii the same, but perhaps once in a while I can do one large agent for something like a boss fight. If even the implementation itself is expensive, then I would be tempted to stick with static radii.

This is technically possible, but it's hard to say for sure. If you have 100–500 agents on the screen and 1 big boss, I believe it won't matter (not tested, just guessing). I think this is not going to be an issue anymore if I manage to implement multi-thread support. 


> Sketching this out I am thinking I would need walls to contain the horde so it doesn't spill out to the sides. I assume this is feasible because your videos contain static obstacles.

> These would be very valuable for game feel I think so I am curious to see how your tests come out for this! I like your implementation ideas.

New challenge then! :) Let me develop something similar so we can see the result. I'm curious to see what will happen when I unleash and explode them in a contained space :D


> If there is something else that you wanted input on but I missed it - please let me know

You didn’t miss it, but is it OK for you to set the game world bounds for agents manually, like 5000x5000, etc.?

