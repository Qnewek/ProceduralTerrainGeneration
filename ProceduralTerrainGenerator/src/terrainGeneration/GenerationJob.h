#pragma once

#include <chrono>
#include <future>
#include <optional>
#include <utility>

//At most one background generation task at a time. The task must work only on data it owns
//(copied or moved into it), so nothing is shared with the main thread until the result is taken.
//A running task is waited for when the job is destroyed.
template<typename Result>
class GenerationJob
{
public:
	//Runs fn() on a new thread; must not be called while IsRunning()
	template<typename F>
	void Start(F&& fn)
	{
		future = std::async(std::launch::async, std::forward<F>(fn));
	}

	//true from Start until the result has been taken
	bool IsRunning() const { return future.valid(); }

	//Returns the result once the task has finished (exactly once), std::nullopt otherwise. Never blocks
	std::optional<Result> TryTake()
	{
		if (!future.valid() || future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
			return std::nullopt;
		}
		return future.get();
	}

private:
	std::future<Result> future;
};
