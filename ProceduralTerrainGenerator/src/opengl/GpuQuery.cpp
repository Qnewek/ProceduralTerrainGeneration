#include "GpuQuery.h"

GpuQuery::GpuQuery(GLenum _target) : target(_target)
{
	glCreateQueries(target, queryCount, ids);
}

GpuQuery::~GpuQuery()
{
	glDeleteQueries(queryCount, ids);
}

void GpuQuery::Begin()
{
	if (pending == queryCount) {
		return;
	}
	glBeginQuery(target, ids[writeIndex]);
	active = true;
}

void GpuQuery::End()
{
	if (!active) {
		return;
	}
	glEndQuery(target);
	active = false;
	writeIndex = (writeIndex + 1) % queryCount;
	pending++;
}

bool GpuQuery::Poll(GLuint64& result)
{
	bool hasResult = false;
	while (pending > 0) {
		GLint available = GL_FALSE;
		glGetQueryObjectiv(ids[readIndex], GL_QUERY_RESULT_AVAILABLE, &available);
		if (available == GL_FALSE) {
			break;
		}
		glGetQueryObjectui64v(ids[readIndex], GL_QUERY_RESULT, &result);
		hasResult = true;
		readIndex = (readIndex + 1) % queryCount;
		pending--;
	}
	return hasResult;
}
