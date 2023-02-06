/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OBLIB_THREAD_MGR_H
#define OBLIB_THREAD_MGR_H

#include "lib/ob_define.h"
#include "lib/ob_running_mode.h"
#include "lib/thread/thread_pool.h"
#include "lib/thread/thread_mgr_interface.h"
#include "lib/thread/ob_simple_thread_pool.h"
#include "lib/thread/ob_thread_name.h"
#include "lib/thread/ob_async_task_queue.h"
#include "lib/queue/ob_dedup_queue.h"
#include "lib/queue/ob_lighty_queue.h"
#include "lib/task/ob_timer.h"

namespace oceanbase {
namespace lib {

using common::ObDedupQueue;

class ThreadCountPair
{
public:
  ThreadCountPair()
    : cnt_(0), mini_mode_cnt_(0)
  {}
  ThreadCountPair(const ThreadCountPair &other, ...)
    : cnt_(other.cnt_), mini_mode_cnt_(other.mini_mode_cnt_)
  {}
  ThreadCountPair(int64_t cnt, int64_t mini_mode_cnt)
    : cnt_(cnt), mini_mode_cnt_(mini_mode_cnt)
  {}
  ThreadCountPair(int64_t dummy)
    : cnt_(0), mini_mode_cnt_(0)
  {}
  int64_t get_thread_cnt() const
  {
    return !is_mini_mode() ? cnt_ : mini_mode_cnt_;
  }
private:
  int64_t cnt_;
  int64_t mini_mode_cnt_;
};

class TGConfig
{
public:
#define TG_DEF(id, ...) static const ThreadCountPair id;
#include "lib/thread/thread_define.h"
#undef TG_DEF
};

namespace TGDefIDs {
  enum TGDefIDEnum
  {
    LIB_START = -1,
  #define TG_DEF(id, ...) id,
  #include "lib/thread/thread_define.h"
  #undef TG_DEF
    LIB_END,
    END = 256,
  };
}

enum class TGScope
{
  INVALID,
  TG_STATIC,
  TG_DYNAMIC,
};

enum class TGType
{
  INVALID,
  REENTRANT_THREAD_POOL,
  THREAD_POOL,
  OB_THREAD_POOL,
  TIMER,
  TIMER_GROUP,
  QUEUE_THREAD,
  DEDUP_QUEUE,
  ASYNC_TASK_QUEUE,
};

class TGCommonAttr
{
public:
  const char *name_;
  const char *desc_;
  enum TGScope scope_;
  enum TGType type_;
  TO_STRING_KV(KCSTRING_(name), KCSTRING_(desc), K_(scope), K_(type));
};

class ITG;
typedef std::function<ITG*()> CreateFunc;
extern CreateFunc create_funcs_[TGDefIDs::END];
extern void init_create_func();
extern void lib_init_create_func();
extern bool create_func_inited_;

class TGHelper : public IRunWrapper
{
public:
  virtual ~TGHelper() {}
  virtual void tg_create_cb(int) = 0;
  virtual void tg_destroy_cb(int) = 0;
  virtual uint64_t id() const = 0;
};

extern TGHelper *&get_tenant_tg_helper();
extern void set_tenant_tg_helper(TGHelper *tg_helper);

class ITG
{
public:
  ITG() : tg_helper_(nullptr), tg_cgroup_(lib::ThreadCGroup::INVALID_CGROUP) {}
  virtual ~ITG() {}
  virtual int thread_cnt() = 0;
  virtual int set_thread_cnt(int64_t) = 0;
  virtual int start() = 0;
  virtual void stop() = 0;
  virtual void wait() = 0;

  virtual int set_runnable(TGRunnable &runnable)
  {
    UNUSED(runnable);
    return common::OB_NOT_SUPPORTED;
  }
  virtual int set_handler(TGTaskHandler &handler)
  {
    UNUSED(handler);
    return common::OB_NOT_SUPPORTED;
  }
  virtual int set_adaptive_strategy(const common::ObAdaptiveStrategy &strategy)
  {
    UNUSED(strategy);
    return common::OB_NOT_SUPPORTED;
  }
  virtual int push_task(void *task)
  {
    UNUSED(task);
    return common::OB_NOT_SUPPORTED;
  }
  virtual void get_queue_num(int64_t &num)
  {
    OB_LOG_RET(ERROR, common::OB_ERR_UNEXPECTED, "unexpected invoke");
    num = 0;
  }
  virtual int push_task(const common::IObDedupTask &task)
  {
    UNUSED(task);
    return common::OB_NOT_SUPPORTED;
  }
  virtual int push_task(share::ObAsyncTask &task)
  {
    UNUSED(task);
    return common::OB_NOT_SUPPORTED;
  }
  virtual int schedule(common::ObTimerTask &task, const int64_t delay, bool repeate = false)
  {
    UNUSEDx(task, delay, repeate);
    return common::OB_NOT_SUPPORTED;
  }
  virtual int schedule(int idx, common::ObTimerTask &task,
                       const int64_t delay, bool repeate = false)
  {
    UNUSEDx(idx, task, delay, repeate);
    return common::OB_NOT_SUPPORTED;
  }
  virtual int task_exist(const common::ObTimerTask &task, bool &exist)
  {
    UNUSEDx(task, exist);
    return common::OB_NOT_SUPPORTED;
  }
  virtual int cancel(const common::ObTimerTask &task)
  {
    UNUSED(task);
    return common::OB_NOT_SUPPORTED;
  }
  virtual int cancel_all()
  {
    return common::OB_NOT_SUPPORTED;
  }
  virtual void set_queue_size(const int64_t qsize)
  {
    UNUSED(qsize);
  }
  TGHelper *tg_helper_;
  lib::ThreadCGroup tg_cgroup_;
  TGCommonAttr attr_;
};

template<enum TGType type>
class TG;

class MyReentrantThread : public share::ObReentrantThread
{
public:
  virtual void run2() override
  {
    runnable_->set_thread_idx(get_thread_idx());
    runnable_->run1();
  }
  int blocking_run() { return ObReentrantThread::blocking_run(); }
  void set_runnable_cond()
  {
    runnable_->cond_ = &(get_cond());
  }
  TGRunnable *runnable_ = nullptr;

};
template<>
class TG<TGType::REENTRANT_THREAD_POOL> : public ITG
{
public:
  TG(ThreadCountPair pair)
    : thread_cnt_(pair.get_thread_cnt())
  {}
  ~TG() { destroy(); }
  int thread_cnt() override { return (int)thread_cnt_; }
  int set_thread_cnt(int64_t thread_cnt)
  {
    int ret = common::OB_SUCCESS;
    if (th_ == nullptr) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      thread_cnt_ = thread_cnt;
      th_->set_thread_count(thread_cnt_);
    }
    return ret;
  }
  int set_runnable(TGRunnable &runnable)
  {
    int ret = common::OB_SUCCESS;
    if (th_ != nullptr) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      th_ = new (buf_) MyReentrantThread();
      th_->runnable_ = &runnable;
      th_->set_runnable_cond();
    }
    return ret;
  }
  int logical_start()
  {
    int ret = common::OB_SUCCESS;
    if (nullptr == th_) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      th_->runnable_->set_stop(false);
      ret = th_->logical_start();
    }
    return ret;
  }
  void logical_stop() 
  {
    if (nullptr != th_) {
      th_->runnable_->set_stop(true);
      th_->logical_stop();
    }
  }
  void logical_wait()
  {
    if (th_ != nullptr) {
      th_->logical_wait();
    }
  }
  int start() override
  {
    int ret = common::OB_SUCCESS;
    if (nullptr == th_) {
      ret = common::OB_ERR_UNEXPECTED;
    } else if(nullptr == th_->runnable_) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      th_->set_run_wrapper(tg_helper_, tg_cgroup_);
      ret = th_->create(thread_cnt_, attr_.name_);
    }
    return ret;
  }
  void stop() override
  {
    if (nullptr != th_) {
      th_->runnable_->set_stop(true);
      th_->stop();
    }
  }
  void wait() override
  {
    if (nullptr != th_) {
      th_->wait();
      destroy();
    }
  }
  void destroy()
  {
    if (th_ != nullptr) {
      th_->destroy();
      th_->~MyReentrantThread();
      th_ = nullptr;
    }
  }
private:
  char buf_[sizeof(MyReentrantThread)];
  MyReentrantThread *th_ = nullptr;
  int thread_cnt_;

};
class MyThreadPool : public lib::ThreadPool
{
public:
  void run1() override
  {
    runnable_->set_thread_idx(get_thread_idx());
    runnable_->run1();
  }
  TGRunnable *runnable_ = nullptr;
};

template<>
class TG<TGType::THREAD_POOL> : public ITG
{
public:
  TG(ThreadCountPair pair)
    : thread_cnt_(pair.get_thread_cnt())
  {}
  ~TG() { destroy(); }
  int thread_cnt() override { return (int)thread_cnt_; }
  int set_thread_cnt(int64_t thread_cnt)
  {
    int ret = common::OB_SUCCESS;
    if (th_ == nullptr) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      thread_cnt_ = thread_cnt;
      th_->set_thread_count(thread_cnt_);
    }
    return ret;
  }
  int set_runnable(TGRunnable &runnable)
  {
    int ret = common::OB_SUCCESS;
    if (th_ != nullptr) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      th_ = new (buf_) MyThreadPool();
      th_->runnable_= &runnable;
    }
    return ret;
  }

  int start() override
  {
    int ret = common::OB_SUCCESS;
    if (nullptr == th_) {
      ret = common::OB_ERR_UNEXPECTED;
    } else if(nullptr == th_->runnable_) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      th_->runnable_->set_stop(false);
      th_->set_thread_count(thread_cnt_);
      th_->set_run_wrapper(tg_helper_, tg_cgroup_);
      ret = th_->start();
    }
    return ret;
  }
  void stop() override
  {
    if (th_ != nullptr) {
      th_->runnable_->set_stop(true);
      th_->stop();
    }
  }
  void wait() override
  {
    if (th_ != nullptr) {
      th_->wait();
      destroy();
    }
  }
  void destroy()
  {
    if (th_ != nullptr) {
      th_->destroy();
      th_->~MyThreadPool();
      th_ = nullptr;
    }
  }
private:
  char buf_[sizeof(MyThreadPool)];
  MyThreadPool *th_ = nullptr;
  int thread_cnt_;
};

class MySimpleThreadPool : public common::ObSimpleThreadPool
{
public:
  void run1() override
  {
    handler_->set_thread_cnt(get_thread_count());
    handler_->set_thread_idx(get_thread_idx());
    common::ObSimpleThreadPool::run1();
  }
  void handle(void *task) override
  {
    handler_->handle(task);
  }
  void handle_drop(void *task) override
  {
    handler_->handle_drop(task);
  }
  TGTaskHandler *handler_ = nullptr;
};

template<>
class TG<TGType::QUEUE_THREAD> : public ITG
{
public:
  TG(ThreadCountPair pair, const int64_t task_num_limit)
    : thread_num_(pair.get_thread_cnt()),
      task_num_limit_(task_num_limit)
  {}
  ~TG() { destroy(); }
  int thread_cnt() override { return (int)thread_num_; }
  int set_thread_cnt(int64_t thread_cnt) override
  {
    int ret = common::OB_SUCCESS;
    if (qth_ == nullptr) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      thread_num_ = thread_cnt;
      qth_->set_thread_count(thread_num_);
    }
    return ret;
  }
  int set_handler(TGTaskHandler &handler)
  {
    int ret = common::OB_SUCCESS;
    uint64_t tenant_id = NULL == tg_helper_ ? common::OB_SERVER_TENANT_ID : tg_helper_->id();
    if (qth_ != nullptr) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      qth_ = new (buf_) MySimpleThreadPool();
      qth_->handler_ = &handler;
      qth_->set_run_wrapper(tg_helper_, tg_cgroup_);
      ret = qth_->init(thread_num_, task_num_limit_, attr_.name_, tenant_id);
    }
    return ret;
  }
  int start() override
  {
    int ret = common::OB_SUCCESS;
    return ret;
  }
  void stop() override
  {
    if (qth_ != nullptr) {
      qth_->stop();
    }
  }
  void wait() override
  {
    if (qth_ != nullptr) {
      qth_->wait();
      destroy();
    }
  }
  void get_queue_num(int64_t &num) override
  {
    num = 0;
    if (qth_ != nullptr) {
      num = qth_->get_queue_num();
    }
  }
  int push_task(void *task) override
  {
    int ret = common::OB_SUCCESS;
    if (OB_ISNULL(qth_)) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      ret = qth_->push(task);
    }
    return ret;
  }
  int set_adaptive_strategy(const common::ObAdaptiveStrategy &strategy) override
  {
    int ret = common::OB_SUCCESS;
    if (OB_ISNULL(qth_)) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      ret = qth_->set_adaptive_strategy(strategy);
    }
    return ret;
  }
  void destroy()
  {
    if (qth_ != nullptr) {
      qth_->destroy();
      qth_->~MySimpleThreadPool();
      qth_ = nullptr;
    }
  }
  void set_queue_size(const int64_t qsize) override
  {
    if (0 != qsize) {
      task_num_limit_ = qsize;
    }
  }
private:
  char buf_[sizeof(MySimpleThreadPool)];
  MySimpleThreadPool *qth_ = nullptr;
  int64_t thread_num_;
  int64_t task_num_limit_;
};

template<>
class TG<TGType::DEDUP_QUEUE> : public ITG
{
public:
  TG(ThreadCountPair pair,
     const int64_t queue_size = ObDedupQueue::TASK_QUEUE_SIZE,
     const int64_t task_map_size = ObDedupQueue::TASK_MAP_SIZE,
     const int64_t total_mem_limit = ObDedupQueue::TOTAL_LIMIT,
     const int64_t hold_mem_limit = ObDedupQueue::HOLD_LIMIT,
     const int64_t page_size = ObDedupQueue::PAGE_SIZE,
     const char *label = nullptr)
    :  thread_num_(pair.get_thread_cnt()),
       queue_size_(queue_size),
       task_map_size_(task_map_size),
       total_mem_limit_(total_mem_limit),
       hold_mem_limit_(hold_mem_limit),
       page_size_(page_size),
       label_(label)
  {}
  ~TG() { destroy(); }
  int thread_cnt() override { return (int)thread_num_; }
  int set_thread_cnt(int64_t thread_cnt) override
  {
    int ret = common::OB_SUCCESS;
    if (qth_ == nullptr) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      thread_num_ = thread_cnt;
      qth_->set_thread_count(thread_num_);
    }
    return ret;
  }
  int start() override
  {
    int ret = common::OB_SUCCESS;
    if (qth_ != nullptr) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      qth_ = new (buf_) common::ObDedupQueue();
      qth_->set_run_wrapper(tg_helper_, tg_cgroup_);
      if (OB_FAIL(qth_->init(thread_num_,
                             attr_.name_,
                             queue_size_,
                             task_map_size_,
                             total_mem_limit_,
                             hold_mem_limit_,
                             page_size_))) {
      } else {
        qth_->set_label(label_);
      }
    }
    return ret;
  }
  void stop() override
  {
    if (qth_ != nullptr) {
      qth_->stop();
    }
  }
  void wait() override
  {
    if (qth_ != nullptr) {
      qth_->wait();
      destroy();
    }
  }
  int push_task(const common::IObDedupTask &task) override
  {
    int ret = common::OB_SUCCESS;
    if (OB_ISNULL(qth_)) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      ret = qth_->add_task(task);
    }
    return ret;
  }
  void get_queue_num(int64_t &num) override
  {
    num = 0;
    if (qth_ != nullptr) {
      num = qth_->task_count();
    }
  }
  void destroy()
  {
    if (qth_ != nullptr) {
      qth_->destroy();
      qth_->~ObDedupQueue();
      qth_ = nullptr;
    }
  }
private:
  char buf_[sizeof(common::ObDedupQueue)];
  common::ObDedupQueue *qth_ = nullptr;
  int32_t thread_num_;
  const int64_t queue_size_;
  const int64_t task_map_size_;
  const int64_t total_mem_limit_;
  const int64_t hold_mem_limit_;
  const int64_t page_size_;
  const char *label_;
};

template<>
class TG<TGType::TIMER> : public ITG
{
public:
  TG(int64_t max_task_num = 32)
    : max_task_num_(max_task_num)
  {}
  ~TG() { destroy(); }
  int thread_cnt() override { return 1; }
  int set_thread_cnt(int64_t thread_cnt) override
  {
    UNUSED(thread_cnt);
    int ret = common::OB_ERR_UNEXPECTED;
    return ret;
  }
  int start() override
  {
    int ret = common::OB_SUCCESS;
    if (timer_ != nullptr) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      timer_ = new (buf_) common::ObTimer(max_task_num_);
      timer_->set_run_wrapper(tg_helper_, tg_cgroup_);
      if (OB_FAIL(timer_->init(attr_.name_))) {
        OB_LOG(WARN, "init failed", K(ret));
      }
    }
    return ret;
  }
  void stop() override
  {
    if (timer_ != nullptr) {
      timer_->stop();
    }
  }
  void wait() override
  {
    if (timer_ != nullptr) {
      timer_->wait();
      destroy();
    }
  }

  int schedule(common::ObTimerTask &task, const int64_t delay, bool repeate = false) override
  {
    int ret = common::OB_SUCCESS;
    if (OB_ISNULL(timer_)) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      ret = timer_->schedule(task, delay, repeate);
    }
    return ret;
  }
  int task_exist(const common::ObTimerTask &task, bool &exist) override
  {
    int ret = common::OB_SUCCESS;
    if (OB_ISNULL(timer_)) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      ret = timer_->task_exist(task, exist);
    }
    return ret;
  }
  int cancel(const common::ObTimerTask &task) override
  {
    int ret = common::OB_SUCCESS;
    if (OB_ISNULL(timer_)) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      ret = timer_->cancel(task);
    }
    return ret;
  }
  int cancel_all() override
  {
    int ret = common::OB_SUCCESS;
    if (OB_ISNULL(timer_)) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      timer_->cancel_all();
    }
    return ret;
  }
  void destroy()
  {
    if (timer_ != nullptr) {
      timer_->destroy();
      timer_->~ObTimer();
      timer_ = nullptr;
    }
  }
private:
  char buf_[sizeof(common::ObTimer)];
  common::ObTimer *timer_ = nullptr;
  int64_t max_task_num_;
};

template<>
class TG<TGType::ASYNC_TASK_QUEUE> : public ITG
{
public:
  TG(lib::ThreadCountPair pair, const int64_t queue_size)
    : thread_cnt_(pair.get_thread_cnt()),
      queue_size_(queue_size)
  {}
  ~TG() { destroy(); }
  int thread_cnt() override { return (int)thread_cnt_; }
  int set_thread_cnt(int64_t thread_cnt) override
  {
    int ret = common::OB_SUCCESS;
    if (qth_ == nullptr) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      thread_cnt_ = thread_cnt;
      qth_->set_thread_count(thread_cnt_);
    }
    return ret;
  }
  int start() override
  {
    int ret = common::OB_SUCCESS;
    if (qth_ != nullptr) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      qth_ = new (buf_) share::ObAsyncTaskQueue();
      qth_->set_run_wrapper(tg_helper_, tg_cgroup_);
      if (OB_FAIL(qth_->init(thread_cnt_,
                             queue_size_,
                             attr_.name_))) {
      } else {
        ret = qth_->start();
      }
    }
    return ret;
  }
  void stop() override
  {
    if (qth_ != nullptr) {
      qth_->stop();
    }
  }
  void wait() override
  {
    if (qth_ != nullptr) {
      qth_->wait();
      destroy();
    }
  }
  int push_task(share::ObAsyncTask &task) override
  {
    int ret = common::OB_SUCCESS;
    if (OB_ISNULL(qth_)) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      ret = qth_->push(task);
    }
    return ret;
  }
  void destroy()
  {
    if (qth_ != nullptr) {
      qth_->destroy();
      qth_->~ObAsyncTaskQueue();
      qth_ = nullptr;
    }
  }
private:
  char buf_[sizeof(share::ObAsyncTaskQueue)];
  share::ObAsyncTaskQueue *qth_ = nullptr;
  int thread_cnt_;
  int64_t queue_size_;
};

template<>
class TG<TGType::TIMER_GROUP> : public ITG
{
  static constexpr int MAX_CNT = 32;
  using TimerType = TG<TGType::TIMER>;
public:
  TG(ThreadCountPair pair)
    : cnt_(pair.get_thread_cnt())
  {}
  ~TG() { destroy(); }
  int thread_cnt() override { return (int)cnt_; }
  int set_thread_cnt(int64_t thread_cnt)
  {
    UNUSED(thread_cnt);
    int ret = common::OB_ERR_UNEXPECTED;
    return ret;
  }
  int start() override
  {
    int ret = common::OB_SUCCESS;
    if (is_inited_) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      if (cnt_ >= MAX_CNT) {
        ret = common::OB_ERR_UNEXPECTED;
      }
      for (int i = 0; OB_SUCC(ret) && i < cnt_; i++) {
        auto *tmp = new (buf_ + sizeof(TimerType) * i) TimerType();
        tmp->attr_ = attr_;
        if (OB_FAIL(tmp->start())) {
        } else {
          timers_[i] = tmp;
        }
      }
      is_inited_ = true;
    }
    return ret;
  }
  void stop() override
  {
    if (is_inited_) {
      for (int i = 0; i < cnt_; i++) {
        if (timers_[i] != nullptr) {
          timers_[i]->stop();
        }
      }
    }
  }
  void wait() override
  {
    if (is_inited_) {
      for (int i = 0; i < cnt_; i++) {
        if (timers_[i] != nullptr) {
          timers_[i]->wait();
        }
      }
      destroy();
    }
  }
  int schedule(int idx, common::ObTimerTask &task, const int64_t delay,
               bool repeate = false) override
  {
    int ret = common::OB_SUCCESS;
    if (!is_inited_) {
      ret = common::OB_ERR_UNEXPECTED;
    } else if (idx >= cnt_) {
      ret = common::OB_INVALID_ARGUMENT;
    } else if (OB_ISNULL(timers_[idx])) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      ret = timers_[idx]->schedule(task, delay, repeate);
    }
    return ret;
  }
  void destroy()
  {
    if (is_inited_) {
      for (int i = 0; i < cnt_; i++) {
        if (timers_[i] != nullptr) {
          timers_[i]->~TimerType();
          timers_[i] = nullptr;
        }
      }
      is_inited_ = false;
    }
  }
private:
  char buf_[sizeof(TimerType) * MAX_CNT];
  TimerType *timers_[MAX_CNT] = {nullptr};
  const int cnt_ = 0;
  bool is_inited_ = false;
};

template<enum TGType type>
class TGCLSMap;
#define BIND_TG_CLS(type, CLS_)    \
  namespace lib                    \
  {                                \
    template<>                     \
    class TGCLSMap<type>           \
    {                              \
    public:                        \
      using CLS = CLS_;            \
    };                             \
  }
} // end of namespace lib

BIND_TG_CLS(TGType::REENTRANT_THREAD_POOL, TG<TGType::REENTRANT_THREAD_POOL>);
BIND_TG_CLS(TGType::THREAD_POOL, TG<TGType::THREAD_POOL>);
BIND_TG_CLS(TGType::QUEUE_THREAD, TG<TGType::QUEUE_THREAD>);
BIND_TG_CLS(TGType::DEDUP_QUEUE, TG<TGType::DEDUP_QUEUE>);
BIND_TG_CLS(TGType::TIMER, TG<TGType::TIMER>);
BIND_TG_CLS(TGType::TIMER_GROUP, TG<TGType::TIMER_GROUP>);
BIND_TG_CLS(TGType::ASYNC_TASK_QUEUE, TG<TGType::ASYNC_TASK_QUEUE>);


namespace lib {

class TGMgr
{
private:
  TGMgr();
  ~TGMgr();
public:
  static TGMgr &instance()
  {
    static TGMgr mgr;
    return mgr;
  }
  int alloc_tg_id(int start = 0);
  void free_tg_id(int tg_id);
  int create_tg(int tg_def_id, int &tg_id, int start_idx = TGDefIDs::END)
  {
    tg_id = -1;
    int ret = common::OB_SUCCESS;
    if (OB_UNLIKELY(!create_func_inited_)) {
      init_create_func();
      create_func_inited_ = true;
    }
    ITG *tg = nullptr;
    auto func = create_funcs_[tg_def_id];
    if (!func) {
      // 不做逻辑，但需要将id占掉
      alloc_tg_id(start_idx);
    } else if (FALSE_IT(tg_id = alloc_tg_id(start_idx))) {
    } else if (tg_id < 0) {
      ret = common::OB_SIZE_OVERFLOW;
    } else if (OB_ISNULL(tg = func())) {
      ret = common::OB_INIT_FAIL;
    } else {
      tgs_[tg_id] = tg;
      OB_LOG(INFO, "create tg succeed",
             "tg_id", tg_id,
             "tg", tg,
             "thread_cnt", tg->thread_cnt(),
             K(tg->attr_));
    }
    return ret;
  }
  // tenant isolation
  int create_tg_tenant(int tg_def_id,
                       int &tg_id,
                       int64_t qsize = 0,
                       lib::ThreadCGroup cgroup = lib::ThreadCGroup::FRONT_CGROUP)
  {
    tg_id = -1;
    int ret = common::OB_SUCCESS;
    if (OB_UNLIKELY(!create_func_inited_)) {
      init_create_func();
      create_func_inited_ = true;
    }
    ITG *tg = nullptr;
    auto func = create_funcs_[tg_def_id];
    if (!func) {
      // 不做逻辑，但需要将id占掉
      alloc_tg_id(TGDefIDs::END);
    } else if (FALSE_IT(tg_id = alloc_tg_id(TGDefIDs::END))) {
    } else if (tg_id < 0) {
      ret = common::OB_SIZE_OVERFLOW;
    } else if (OB_ISNULL(tg = func())) {
      ret = common::OB_INIT_FAIL;
    } else {
      TGHelper *tg_helper = get_tenant_tg_helper();
      // 目前只打印日志
      if (OB_ISNULL(tg_helper)) {
        OB_LOG(WARN, "create tg tenant but tenant tg helper is null", K(tg_def_id), K(tg_id));
      } else {
        tg->tg_helper_ = tg_helper;
        tg->tg_cgroup_ = cgroup;
        tg_helper->tg_create_cb(tg_id);
      }
      tg->set_queue_size(qsize);
      tgs_[tg_id] = tg;
      OB_LOG(INFO, "create tg succeed",
             "tg_id", tg_id,
             "tg", tg,
             "thread_cnt", tg->thread_cnt(),
             K(tg->attr_), KP(tg));
    }
    return ret;
  }
  void destroy_tg(int tg_id, bool is_exist = false);
private:
  static constexpr int MAX_ID = 12288;
  common::ObLatchMutex lock_;
  ABitSet bs_;
  char bs_buf_[ABitSet::buf_len(MAX_ID)];
public:
  ITG *tgs_[MAX_ID] = {nullptr};
  int default_tg_id_map_[TGDefIDs::END] = {-1};
};

template<typename Func, bool return_void=false>
class FWrap
{
public:
  int operator()(Func func)
  {
    return func();
  }
};
template<typename Func>
class FWrap<Func, true>
{
public:
  int operator()(Func func)
  {
    func();
    return common::OB_SUCCESS;
  }
};

#define TG_INVOKE(tg_id, func, args...)              \
  ({                                                 \
    int ret = common::OB_SUCCESS;                    \
    if (OB_UNLIKELY(-1 == tg_id)) {                  \
      ret = common::OB_ERR_UNEXPECTED;               \
      OB_LOG(ERROR, "invalid tg id");                \
    } else {                                         \
      lib::ITG *tg =                                 \
        TG_MGR.tgs_[static_cast<int>(tg_id) < static_cast<int>(lib::TGDefIDs::END) ? \
        TG_MGR.default_tg_id_map_[tg_id] : tg_id];   \
      if (OB_ISNULL(tg)) {                           \
        ret = common::OB_ERR_UNEXPECTED;             \
      } else {                                       \
        auto f = [&]() { return tg->func(args); };   \
        ret = lib::FWrap<decltype(f), std::is_void<decltype(f())>::value>()(f); \
      }                                              \
    }                                                \
    ret;                                             \
  })

// tenant scope
#define TG_MGR (lib::TGMgr::instance())
#define TG_CREATE(tg_def_id, tg_id) TG_MGR.create_tg(tg_def_id, tg_id)
#define TG_CREATE_TENANT(tg_def_id, args...) TG_MGR.create_tg_tenant(tg_def_id, args)
#define TG_DESTROY(tg_id) TG_MGR.destroy_tg(tg_id)
#define TG_SET_RUNNABLE(tg_id, args...) TG_INVOKE(tg_id, set_runnable, args)
#define TG_SET_HANDLER(tg_id, args...) TG_INVOKE(tg_id, set_handler, args)
#define TG_START(tg_id)                                    \
  ({                                                       \
    const char *tg_name = nullptr;                         \
    if (TG_MGR.tgs_[tg_id] != nullptr) {                   \
      tg_name = TG_MGR.tgs_[tg_id]->attr_.name_;           \
    }                                                      \
    OB_LOG(INFO, "start tg", K(tg_id), KCSTRING(tg_name)); \
    int ret = TG_INVOKE(tg_id, start);                     \
    ret;                                                   \
  })
#define TG_SET_ADAPTIVE_STRATEGY(tg_id, args...) TG_INVOKE(tg_id, set_adaptive_strategy, args)
#define TG_PUSH_TASK(tg_id, args...) TG_INVOKE(tg_id, push_task, args)
#define TG_GET_QUEUE_NUM(tg_id, args...) TG_INVOKE(tg_id, get_queue_num, args)
#define TG_GET_THREAD_CNT(tg_id) TG_INVOKE(tg_id, thread_cnt)
#define TG_SET_THREAD_CNT(tg_id, count) TG_INVOKE(tg_id, set_thread_cnt, count)
#define TG_WAIT_R(tg_id) TG_INVOKE(tg_id, wait)
#define TG_WAIT(tg_id) do { int r = TG_INVOKE(tg_id, wait); UNUSED(r); } while (0)
#define TG_STOP_R(tg_id) TG_INVOKE(tg_id, stop)
#define TG_STOP(tg_id) do { int r = TG_INVOKE(tg_id, stop); UNUSED(r); } while (0)
#define TG_CANCEL_R(tg_id, args...) TG_INVOKE(tg_id, cancel, args)
#define TG_CANCEL(tg_id, args...) do { int r = TG_INVOKE(tg_id, cancel, args); UNUSED(r); } while (0)
#define TG_CANCEL_ALL(tg_id) TG_INVOKE(tg_id, cancel_all)
#define TG_TASK_EXIST(tg_id, args...) TG_INVOKE(tg_id, task_exist, args)
#define TG_SCHEDULE(tg_id, args...) TG_INVOKE(tg_id, schedule, args)
#define TG_EXIST(tg_id) (TG_MGR.tgs_[tg_id] != nullptr)
#define TG_SET_RUNNABLE_AND_START(tg_id, args...) \
  ({                                              \
    int ret = OB_SUCCESS;                         \
    ret = TG_SET_RUNNABLE(tg_id, args);           \
    if (OB_SUCC(ret)) {                           \
      ret = TG_START(tg_id);                      \
    }                                             \
    ret;                                          \
  })
#define TG_SET_HANDLER_AND_START(tg_id, args...) \
  ({                                             \
    int ret = OB_SUCCESS;                        \
    ret = TG_SET_HANDLER(tg_id, args);           \
    if (OB_SUCC(ret)) {                          \
      ret = TG_START(tg_id);                     \
    }                                            \
    ret;                                         \
  })

#define TG_REENTRANT_LOGICAL_START(tg_id)                                                               \
  ({                                                                                                    \
    int ret = OB_SUCCESS;                                                                               \
    enum TGType tg_type = TGType::INVALID;                                                              \
    ITG* tg = TG_MGR.tgs_[tg_id];                                                                       \
    if (nullptr != tg) {                                                                                \
      tg_type = tg->attr_.type_;                                                                        \
    }                                                                                                   \
    if (TGType::REENTRANT_THREAD_POOL == tg_type) {                                                     \
      TG<TGType::REENTRANT_THREAD_POOL>* tmp_tg = static_cast<TG<TGType::REENTRANT_THREAD_POOL>*>(tg); \
      ret = tmp_tg->logical_start();                                                                    \
    } else {                                                                                            \
      ret = common::OB_ERR_UNEXPECTED;                                                                  \
      OB_LOG(WARN, "logical start only can be used with REENTRANT_THREAD_POOL");                        \
    }                                                                                                   \
    ret;                                                                                                \
  })

#define TG_REENTRANT_LOGICAL_STOP(tg_id)                                                                \
  ({                                                                                                    \
    int ret = OB_SUCCESS;                                                                               \
    enum TGType tg_type = TGType::INVALID;                                                              \
    ITG* tg = TG_MGR.tgs_[tg_id];                                                                       \
    if (nullptr != tg) {                                                                                \
      tg_type = tg->attr_.type_;                                                                        \
    }                                                                                                   \
    if (TGType::REENTRANT_THREAD_POOL == tg_type) {                                                     \
      TG<TGType::REENTRANT_THREAD_POOL>* tmp_tg = static_cast<TG<TGType::REENTRANT_THREAD_POOL>*>(tg); \
      tmp_tg->logical_stop();                                                                           \
    } else {                                                                                            \
      ret = common::OB_ERR_UNEXPECTED;                                                                  \
      OB_LOG(WARN, "logical stop only can be used with REENTRANT_THREAD_POOL");                         \
    }                                                                                                   \
  })

#define TG_REENTRANT_LOGICAL_WAIT(tg_id)                                                                \
  ({                                                                                                    \
    int ret = OB_SUCCESS;                                                                               \
    enum TGType tg_type = TGType::INVALID;                                                              \
    ITG* tg = TG_MGR.tgs_[tg_id];                                                                       \
    if (nullptr != tg) {                                                                                \
      tg_type = tg->attr_.type_;                                                                        \
    }                                                                                                   \
    if (TGType::REENTRANT_THREAD_POOL == tg_type) {                                                     \
      TG<TGType::REENTRANT_THREAD_POOL>* tmp_tg = static_cast<TG<TGType::REENTRANT_THREAD_POOL>*>(tg); \
      tmp_tg->logical_wait();                                                                           \
    } else {                                                                                            \
      ret = common::OB_ERR_UNEXPECTED;                                                                  \
      OB_LOG(WARN, "logical stop only can be used with REENTRANT_THREAD_POOL");                         \
    }                                                                                                   \
  }) 
} // end of namespace lib
} // end of namespace oceanbase

#endif /* OBLIB_THREAD_MGR_H */
