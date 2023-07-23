#include <mutex>

using namespace std::chrono_literals;

// This should all be doable without going through futures and promises
// How much of the cpp features can I remove from my multithreading and still have it work?

typedef void concurrent_taskqueue_callback(void *taskInfo);

struct taskinfo_wrapper {
	void *taskInfo_;
	concurrent_taskqueue_callback *taskFunction_;
};

struct concurrent_taskinfo_queue {
	taskinfo_wrapper *tasks_;
	size_t volatile startingMaxSize_;
	size_t volatile maxSize_;
	size_t volatile currentSize_;
	size_t volatile currentHead_;
	std::mutex mutex_;
	std::condition_variable cv_;
	bool interrupt_;
};

void InitConcurrentTaskInfoQueue(concurrent_taskinfo_queue *queue, size_t maxSize) {
	queue->tasks_ = (taskinfo_wrapper *)malloc(maxSize * sizeof(taskinfo_wrapper));
	queue->maxSize_ = maxSize;
	queue->startingMaxSize_ = maxSize;
	queue->currentSize_ = 0;
	queue->currentHead_ = 0;
	queue->interrupt_ = false;
}

void ResetConcurrentTaskQueue(concurrent_taskinfo_queue *queue) {
	free(queue->tasks_);
	queue->tasks_ = (taskinfo_wrapper *)malloc((queue->startingMaxSize_) * sizeof(taskinfo_wrapper));
	queue->maxSize_ = queue->startingMaxSize_;
	queue->currentSize_ = 0;
	queue->currentHead_ = 0;
	queue->interrupt_ = false;
}


void PushTaskToQueue(void *taskInfo, concurrent_taskqueue_callback *taskFunction, concurrent_taskinfo_queue *queue) {
	queue->mutex_.lock();
	if (queue->currentSize_ == queue->maxSize_) {
		size_t newSize = queue->maxSize_ * 2 + 1;
		taskinfo_wrapper *newTasks = reinterpret_cast<taskinfo_wrapper *>(realloc(queue->tasks_, newSize * sizeof(taskinfo_wrapper)));
		if (!newTasks) {
			exit(1);
		}
		queue->tasks_ = newTasks;
		queue->maxSize_ = newSize;
	}
	taskinfo_wrapper *nextTask = queue->tasks_ + queue->currentSize_;
	nextTask->taskInfo_ = taskInfo;
	nextTask->taskFunction_ = taskFunction;
	queue->currentSize_++;
	queue->cv_.notify_one();
	queue->mutex_.unlock();
}

inline bool IsQueueEmpty(concurrent_taskinfo_queue *queue) {
	return (queue->currentHead_ == queue->currentSize_);
}

volatile taskinfo_wrapper *PopTaskFromQueue(concurrent_taskinfo_queue *queue) {
	std::unique_lock<std::mutex> lk(queue->mutex_);
	while (!(queue->interrupt_) && IsQueueEmpty(queue)) {
		queue->cv_.wait(lk);
	}
	if ((queue->interrupt_)) {
		return nullptr;
	}
	if (IsQueueEmpty(queue)) {
		return nullptr;
	}
	volatile taskinfo_wrapper *returnTask = queue->tasks_ + queue->currentHead_;
	queue->currentHead_++;
	return returnTask;
}

void InterruptConcurrentTaskInfoQueue(concurrent_taskinfo_queue *input) {
	input->mutex_.lock();
	input->interrupt_ = true;
	input->mutex_.unlock();
	input->cv_.notify_all();
}


taskinfo_wrapper *TryPopTaskFromQueue(concurrent_taskinfo_queue *queue) {
	queue->mutex_.lock();
	if (IsQueueEmpty(queue)) {
		queue->mutex_.unlock();
		return nullptr;
	}
	taskinfo_wrapper *resultTask = queue->tasks_ + queue->currentHead_++;
	queue->mutex_.unlock();
	return resultTask;
}


struct thread_pool {
	std::thread *threads_ = nullptr;
	volatile size_t nTotalThreads_ = 0;
	volatile bool isActive_ = false;
	volatile bool isInterrupt_ = false;
	concurrent_taskinfo_queue queue_;
};


void RunThreadFunc(thread_pool *pool) {
	while (!(pool->isInterrupt_)) {
		volatile taskinfo_wrapper *taskWrapper = PopTaskFromQueue(&(pool->queue_));
		if (!(pool->isInterrupt_) && !!(taskWrapper)) {
			taskWrapper->taskFunction_(taskWrapper->taskInfo_);
		}
	}
}

void InitThreadPool(thread_pool *pool, size_t initQueueSize, size_t nThreads) {
	pool->isInterrupt_ = false;
	if (pool->threads_) {
		free(pool->threads_);
	}
	pool->threads_ = (std::thread *)malloc(nThreads * sizeof(std::thread));
	pool->nTotalThreads_ = nThreads;
	for (size_t threadNumber = 0; threadNumber < nThreads; threadNumber++) {
		new(pool->threads_ + threadNumber) std::thread(RunThreadFunc, pool);
	}
	InitConcurrentTaskInfoQueue(&(pool->queue_), initQueueSize);
	pool->isActive_ = true;
}

void StopThreadPool(thread_pool *pool) {
	if (pool->isActive_) {
		pool->isInterrupt_ = true;
		InterruptConcurrentTaskInfoQueue(&(pool->queue_));
	}
	for (size_t threadNumber = 0; threadNumber < pool->nTotalThreads_; threadNumber++) {
		pool->threads_[threadNumber].join();
	}
	free(pool->threads_);
	pool->isActive_ = false;
	pool->isInterrupt_ = false;
	pool->queue_.interrupt_ = false;
	if (pool->queue_.tasks_) {
		free(pool->queue_.tasks_);
	}
}

bool ThreadPoolActiveWait(thread_pool *pool) {
	bool iDidWork = false;
	while (!IsQueueEmpty(&(pool->queue_))) {
		if (volatile taskinfo_wrapper *taskWrapper = TryPopTaskFromQueue(&(pool->queue_))) {
			taskWrapper->taskFunction_(taskWrapper->taskInfo_);
			iDidWork = true;
		}
	}
	return iDidWork;
}
