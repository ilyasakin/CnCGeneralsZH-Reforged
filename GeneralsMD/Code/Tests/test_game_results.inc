// The game-results queue's one worker (GameResultsThread.cpp) sleeps until addRequest wakes it, where it
// used to wake every millisecond to look.  A request it is never woken for would sit in the queue until the
// next one, so this checks that one is served, and that ending the queue wakes the worker and joins it at
// once.  The request's host is a literal address, so nothing is looked up; off Windows the send itself is a
// stub, and on Windows it is refused by the local machine.

#include "GameNetwork/GameSpy/GameResultsThread.h"

#include <chrono>
#include <thread>

TEST(game_results_worker_serves_a_request_and_leaves_when_woken)
{
	CHECK( bootOnce() );
	GameResultsInterface *queue = GameResultsInterface::createNewGameResultsInterface();
	CHECK( queue != NULL );
	if (queue == NULL)
		return;

	// Give the worker time to be asleep, so the request has to wake it.
	std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
	CHECK( queue->areThreadsRunning() );

	GameResultsRequest request;
	request.hostname = "127.0.0.1";
	request.port = 9;
	request.results = "results";
	queue->addRequest( request );
	std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

	const std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
	queue->endThreads();
	const long long endMs = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - before ).count();
	CHECK( endMs < 1000 );
	CHECK( !queue->areThreadsRunning() );

	// No worker now, so the queue holds still: the request is gone only if the worker took it.
	GameResultsRequest left;
	CHECK( !queue->getRequest( left ) );

	delete queue;
}
