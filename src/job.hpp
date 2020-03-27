#pragma once

namespace mag {

/// A Job represents a task to be performed in the background.
struct Job {
    /// Run one tick of the job.  Returns if the job has halted.
    ///
    /// When true is return, the job is removed from the work queue.  When this occurs, the tick
    /// function should clean up the Job, unlocking held locks and deallocating memory.
    bool (*tick)(void* data);
    void* data;
};

}
