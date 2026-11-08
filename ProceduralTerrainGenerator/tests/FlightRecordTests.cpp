#include "doctest/doctest.h"

#include "FlightRecord.h"

TEST_CASE("FlightRecord ignores frames until started and after stopped")
{
	FlightRecord record;
	record.Add(10.0, glm::vec3(0.0f), true);
	CHECK(record.Empty());
	record.Start();
	record.Add(10.0, glm::vec3(0.0f), false);
	record.Stop();
	record.Add(10.0, glm::vec3(0.0f), true);
	CHECK(record.Frames() == 1);
	CHECK(record.HoleFrames() == 0);
}

TEST_CASE("FlightRecord gives frame statistics")
{
	FlightRecord record;
	record.Start();
	for (int i = 0; i < 99; i++) {
		record.Add(10.0, glm::vec3(0.0f), i < 3);
	}
	record.Add(50.0, glm::vec3(0.0f), false);
	CHECK(record.Frames() == 100);
	CHECK(record.Seconds() == doctest::Approx(1.04));
	CHECK(record.AverageFps() == doctest::Approx(100.0 / 1.04));
	CHECK(record.WorstFrameMs() == 50.0);
	CHECK(record.Percentile99Ms() == 50.0);
	CHECK(record.SlowFrames() == 1);
	CHECK(record.HoleFrames() == 3);
}

TEST_CASE("FlightRecord measures camera speed")
{
	FlightRecord record;
	record.Start();
	//First second at 100 units/s, next at 300 units/s, frames of 10 ms
	glm::vec3 camera(0.0f);
	record.Add(10.0, camera, false);
	for (int i = 0; i < 100; i++) {
		camera.x += 1.0f;
		record.Add(10.0, camera, false);
	}
	for (int i = 0; i < 100; i++) {
		camera.z += 3.0f;
		record.Add(10.0, camera, false);
	}
	CHECK(record.MaxSpeed() == doctest::Approx(300.0));
	CHECK(record.AverageSpeed() == doctest::Approx(400.0 / 2.01));
}

TEST_CASE("FlightRecord starts over")
{
	FlightRecord record;
	record.Start();
	record.Add(30.0, glm::vec3(0.0f), true);
	record.Start();
	CHECK(record.Empty());
	CHECK(record.IsRecording());
	CHECK(record.HoleFrames() == 0);
}
