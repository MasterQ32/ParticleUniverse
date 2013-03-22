#ifndef _PARTICLES_H_
#define _PARTICLES_H_

#define PI 3.1415926535897932384626433832795

///
/// Particle definition
///
typedef struct
{
	int type;
	float mass;
	float density;
	int pad1;
	float2 position;
	float2 velocity;
	int flags;
	int pad2;
} Particle;

///
/// If this flag is set, the particle has a fixed position
///
#define PARTICLE_FIXED		(1<<0)

///
/// If this flag is set, the particle has an attracting force on other particles
///
#define PARTICLE_ATTRACTOR	(1<<1)

///
/// If this flag is set, the particle will destroy other particles which collide with it.
///
#define PARTICLE_CRUNCHER	(1<<2)

#endif