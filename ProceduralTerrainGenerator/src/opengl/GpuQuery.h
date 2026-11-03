#pragma once

#include <GL/glew.h>

//Asynchronous OpenGL query that never stalls the pipeline: results are read only once the GPU has them.
//Keeps a few query objects because results arrive some frames after the measured commands were sent.
class GpuQuery
{
public:
	//@param _target - query type, e.g. GL_TIME_ELAPSED or GL_PRIMITIVES_GENERATED
	explicit GpuQuery(GLenum _target);
	~GpuQuery();

	GpuQuery(const GpuQuery&) = delete;
	GpuQuery& operator=(const GpuQuery&) = delete;

	//Starts measuring. If all query objects still wait for results, this measurement is skipped.
	//Only one query of a given target can be active at a time.
	void Begin();
	void End();

	//Reads all finished measurements
	//@param result - receives the newest result, left unchanged if none is ready
	//@return true if a result was read
	bool Poll(GLuint64& result);

private:
	static constexpr int queryCount = 6;

	GLenum target;
	GLuint ids[queryCount] = {};
	int writeIndex = 0;
	int readIndex = 0;
	int pending = 0;
	bool active = false;
};
