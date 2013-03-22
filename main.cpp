#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <Windows.h>
#include <var.h>
#include <adll.h>
#include <CL\cl.h>

char platformName[256];
char deviceName[256];
char buildLog[4096];

/*
	Typedefs for having same header in OpenCL C and C++
*/
typedef cl_float2 float2;
typedef cl_float4 float4;

#include "particles.h"

/*
	OpenCL Variables
*/
cl_platform_id platform;
cl_device_id device;
cl_context context;
cl_command_queue queue;
cl_program program;

cl_mem memoryPixelBuffer;
cl_mem memoryParticles;

cl_kernel kernelSimulation;
cl_kernel kernelRendererBackground;
cl_kernel kernelRendererParticles;

BMAP *bmpPixelBuffer;
BMAP *bmpBackground;
int imageSize;
int imageWidth;
int imageHeight;
int imagePitch;


int simulationDepth = 1;

const int particleCount = 500;
Particle particles[particleCount];

float zoom = 1.0f;
int zoomStage = 0;

int errCode;

void pfn_notify(const char * errinfo, const void * private_info, size_t cb, void *user_data)
{
	if(user_data != NULL)
		printf("%s: %s\n", user_data, errinfo);
	else
		printf("Context Error: %s\n", errinfo);
}

float rndfloat(float min, float max)
{
	float d = (float)rand() / RAND_MAX;
	return min + d * (max - min);
}

int main(int argc, char **argv)
{
	ENGINE_VARS *ev = engine_open("", NULL);
	video_set(800, 600, 32, 2);
	// Render one image and load DirectX
	engine_frame();
	*ev->fps_max = 60;

	// Setup image and image data
	bmpBackground = bmap_createblack(800, 600, 888);
	bmpPixelBuffer = bmap_createblack(800, 600, 14444);

	var format = bmap_lock(bmpPixelBuffer, 0);

	imageWidth = bmpPixelBuffer->width;
	imageHeight = bmpPixelBuffer->height;
	imagePitch = bmpPixelBuffer->finalwidth; // Real image width im memory
	imageSize = imageHeight * imagePitch * bmpPixelBuffer->bytespp;

	bmap_unlock(bmpPixelBuffer);


	
	clGetPlatformIDs(1, &platform, NULL);
	clGetPlatformInfo(platform, CL_PLATFORM_NAME, 256, platformName, NULL);

	clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
	clGetDeviceInfo(device, CL_DEVICE_NAME, 256, deviceName, NULL);

	context = clCreateContext(NULL, 1, &device, NULL, NULL, NULL);

	queue = clCreateCommandQueue(context, device, NULL, NULL);

	// Debug allows the kernel in the project folder, release next to the executable
#ifdef _DEBUG
	FILE *file = fopen("..\\kernel.cu", "rb");
#else
	FILE *file = fopen("kernel.cu", "rb");
#endif
	fseek(file, 0, SEEK_END);
	size_t sourceLength = ftell(file);
	rewind(file);

	char *source = new char[sourceLength + 1];
	memset(source, 0, sizeof(char) * (sourceLength + 1));
	fread(source, sizeof(char) * sourceLength, 1, file);
	fclose(file);

	program = clCreateProgramWithSource(context, 1, (const char **)&source, &sourceLength, &errCode);

	// Allows inclusion of particles.h if it lays if ".."
	errCode = clBuildProgram(program, 1, &device, "-I . -I ..", NULL, NULL);

	cl_build_status status;
	do
	{
		errCode = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_STATUS, sizeof(status), &status, NULL);
	} while(status == CL_BUILD_IN_PROGRESS);
	
	errCode = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, sizeof(buildLog), buildLog, NULL);
	printf("Build Log: %s\n", buildLog);

	if(status != CL_BUILD_SUCCESS)
	{
		int tmp;
		printf("Build failed!");
		scanf("%d", &tmp);
		goto exit;
	}

	kernelSimulation = clCreateKernel(program, "simulate", NULL);
	kernelRendererBackground = clCreateKernel(program, "render_background", NULL);
	kernelRendererParticles = clCreateKernel(program, "render_particles", NULL);

	memset(particles, 0, sizeof(particles));

	// Start setup of particles

	for(int i = 0; i < particleCount; i++)
	{
		particles[i].type = 1;
		particles[i].mass = rndfloat(10.0f, 200.0f);
		particles[i].density = 10.0f;
		particles[i].position.s[0] = rndfloat(-1000, 1000);
		particles[i].position.s[1] = rndfloat(-1000, 1000);
		particles[i].velocity.s[0] = rndfloat(-1, 1);
		particles[i].velocity.s[1] = rndfloat(-1, 1);
		particles[i].flags = PARTICLE_CRUNCHER | PARTICLE_ATTRACTOR;
	}
	particles[0].mass = 10000;
	particles[0].density = 50.0f;
	particles[0].position.s[0] = 0;
	particles[0].position.s[1] = 0;
	particles[0].flags = PARTICLE_FIXED | PARTICLE_ATTRACTOR | PARTICLE_CRUNCHER;

	memoryPixelBuffer = clCreateBuffer(context, CL_MEM_READ_WRITE, imageSize, NULL, &errCode);
	memoryParticles = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, sizeof(particles), particles, &errCode);

	clSetKernelArg(kernelRendererBackground, 0, sizeof(cl_mem), &memoryPixelBuffer);
	clSetKernelArg(kernelRendererBackground, 1, sizeof(int), &imageWidth);
	clSetKernelArg(kernelRendererBackground, 2, sizeof(int), &imageHeight);
	clSetKernelArg(kernelRendererBackground, 3, sizeof(int), &imagePitch);

	clSetKernelArg(kernelRendererParticles, 0, sizeof(cl_mem), &memoryPixelBuffer);
	clSetKernelArg(kernelRendererParticles, 1, sizeof(int), &imageWidth);
	clSetKernelArg(kernelRendererParticles, 2, sizeof(int), &imageHeight);
	clSetKernelArg(kernelRendererParticles, 3, sizeof(int), &imagePitch);
	clSetKernelArg(kernelRendererParticles, 4, sizeof(cl_mem), &memoryParticles);
	clSetKernelArg(kernelRendererParticles, 5, sizeof(int), &particleCount);
	clSetKernelArg(kernelRendererParticles, 6, sizeof(float), &zoom);

	float stepwidth = 1.0f / simulationDepth;

	clSetKernelArg(kernelSimulation, 0, sizeof(cl_mem), &memoryParticles);
	clSetKernelArg(kernelSimulation, 1, sizeof(int), &particleCount);
	clSetKernelArg(kernelSimulation, 2, sizeof(float), &stepwidth);

	// As long as the engine is opened or escape is not pressed
	while(engine_frame() && *ev->key_esc == NULL)
	{
		cl_event simulateEvent = 0;
		cl_event renderBackgroundEvent;
		cl_event renderParticlesEvent;
		cl_event readImageEvent;

		// Work size for rendering image
		size_t pixelWorkItems[2];
		pixelWorkItems[0] = imageWidth;
		pixelWorkItems[1] = imageHeight;

		errCode = clEnqueueNDRangeKernel(
			queue,
			kernelRendererBackground,
			2,
			NULL,
			pixelWorkItems,
			NULL,
			0,
			NULL,
			&renderBackgroundEvent);

		int evCount = 1;
		if(*ev->key_space == 0)
		{
			// Simulate multiple steps in one frame
			int numSteps = simulationDepth;

			// Speed x4
			if(*ev->key_shift != NULL)
				numSteps = 4 * numSteps;
			for(int i = 0; i < numSteps; i++)
			{
				errCode = clEnqueueNDRangeKernel(
					queue,
					kernelSimulation,
					1,
					NULL,
					(size_t*)&particleCount,
					NULL,
					0,
					NULL,
					&simulateEvent);
				clWaitForEvents(1, &simulateEvent);
			}
		}

		cl_event waitList[] = {renderBackgroundEvent, simulateEvent};
		
		// Render particles
		zoomStage = max(0, zoomStage + ev->mickey->z / 120);
		zoom = 1 / pow(2.0f, zoomStage);
		clSetKernelArg(kernelRendererParticles, 6, sizeof(float), &zoom);
		errCode = clEnqueueNDRangeKernel(
			queue,
			kernelRendererParticles,
			1,
			NULL,
			(size_t*)&particleCount,
			NULL,
			evCount,
			waitList,
			&renderParticlesEvent);

		format = bmap_lock(bmpPixelBuffer, 0);
		
		// Copy image from GRAM to RAM into backbuffer
		cl_float4 *backbuffer = (cl_float4*)bmpPixelBuffer->finalbits;
		errCode = clEnqueueReadBuffer(
			queue, 
			memoryPixelBuffer, 
			CL_TRUE, 
			0,
			imageSize, 
			backbuffer,
			1, 
			&renderParticlesEvent, 
			&readImageEvent);
		bmap_unlock(bmpPixelBuffer);

		// Using rendertarget to get a "smooth" rendering effect
		bmap_rendertarget(bmpBackground, 0, 0);
		draw_quad(
			bmpPixelBuffer,
			vector(0, 0, 0),
			NULL,
			vector(800, 600, 0),
			NULL,
			NULL,
			50,
			0);
		bmap_rendertarget(NULL, 0, 0);

		draw_quad(
			bmpBackground,
			vector(0, 0, 0),
			NULL,
			vector(800, 600, 0),
			NULL,
			NULL,
			100,
			0);

		// Draw debug information
		draw_text(platformName, 16, 16, (COLOR*)vector(255, 0, 0));
		draw_text(deviceName, 16, 32, (COLOR*)vector(255, 0, 0));

		char zoomInfo[128];
		sprintf(zoomInfo, "Zoom = 1:%d", (int)pow(2.0f, zoomStage));
		draw_text(zoomInfo, 16, 48, (COLOR*)vector(255, 0, 0));
	}

exit:
	clReleaseMemObject(memoryPixelBuffer);
	clReleaseKernel(kernelSimulation);
	clReleaseProgram(program);
	clReleaseCommandQueue(queue);
	clReleaseContext(context);

	engine_close();
	return 0;
}