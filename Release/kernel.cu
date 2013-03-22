#include "particles.h"

const float gravConst = 0.0125f;

///
/// Returns radius defined by mass and density
///
float getRadius(__global Particle *p)
{
	float area = p->mass / p->density;
	return sqrt(area / PI);
}

///
/// Particle simulation step 
///
void __kernel simulate(
	__global Particle *particles,
	int particleCount,
	float stepWidth)
{
	int i;
	int id = get_global_id(0);
	if(id >= particleCount)
		return;
	if(particles[id].type == 0)
		return;
	
	__global Particle *p = &particles[id];

	float radius = getRadius(p);

	
	float2 force = (float2)(0, 0);
	for(i = 0; i < particleCount; i++)
	{
		// Ignore ourselfs
		if(i == id)
			continue;

		// Ignore empty particles
		if(particles[i].type == 0)
			continue;
			
		float2 dir = particles[i].position - p->position;
		float dist = length(dir);

		if((particles[i].flags & PARTICLE_CRUNCHER) != 0)
		{
			// Check if we ran into a particle cruncher (simple circle/point intersection)
			if(dist < (max(getRadius(&particles[i]), radius)))
			{
				bool getCrunch = true;
				// Check if we are a particle cruncher and heavier...
				if((p->flags & PARTICLE_CRUNCHER) != 0 && p->mass >= particles[i].mass)
				{
					// Prevent getting chruched
					if(p->mass == particles[i].mass)
					{
						// We have same mass, let the first survive
						if(i > id)
							getCrunch = false;
					}
					else
					{
						// We are heavier, we survive
						getCrunch = false;
					}
				}

				if(getCrunch)
				{
					p->type = 0;

					// Add velocity delta with conservation of momentum
					particles[i].velocity += (particles[i].velocity * particles[i].mass + p->velocity * p->mass) / (particles[i].mass + p->mass) - particles[i].velocity;
					particles[i].mass += p->mass;
					return;
				}
			}
		}

		// Ignore all nonattracting particles from here (gravity calculation)
		if((particles[i].flags & PARTICLE_ATTRACTOR) == 0)
			continue;

		if(dist == 0)
			continue;

		dir = normalize(dir);

		// Calculate gravitational force and add it
		force += gravConst * ((particles[i].mass * p->mass) / dist) * dir;
	}

	// Check if the particle allows movement
	if((p->flags & PARTICLE_FIXED) == 0)
	{
		// Accelerate the particle
		p->velocity += (force / p->mass) * (1 / 60.0f) * stepWidth;

		// Then move it
		p->position += p->velocity;
	}
}

///
/// Renders all particles to the backbuffer
///
void __kernel render_particles(
	__global float4 *image,
	int width,
	int height,
	int pitch,
	__global Particle *particles,
	int particleCount,
	float zoom)
{
	int id = get_global_id(0);
	if(id >= particleCount)
		return;
	if(particles[id].type == 0)
		return;
	int px = (int)(0.5f * width + zoom * particles[id].position.x);
	int py = (int)(0.5f * height + zoom * particles[id].position.y);
	int radius = max((int)(zoom * getRadius(&particles[id]) + 0.5), 0);
	
	for(int x = px - radius; x <= px + radius; x++)
	{
		for(int y = py - radius; y <= py + radius; y++)
		{
			if(x < 0 || y < 0)
				continue;
			if(x >= width || y >= height)
				continue;
			if(sqrt((float)(x-px)*(x-px) + (y-py)*(y-py)) > radius)
				continue;
			if(particles[id].flags & PARTICLE_FIXED)
				image[pitch * y + x] = (float4)(1.0f, 0.0f, 0.0f, 1.0f);
			else
				image[pitch * y + x] = (float4)(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}
}

///
/// Clears the back buffer to black
///
void __kernel render_background(
	__global float4 *image,
	int width,
	int height,
	int pitch)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	if(x >= width || y >= height)
		return;
	float4 color = (float4)(0.0f, 0.0f, 0.0f, 1.0f);
	image[pitch * y + x] = color;
}