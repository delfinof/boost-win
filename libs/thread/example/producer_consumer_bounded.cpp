// (C) Copyright 2012 Howard Hinnant
// (C) Copyright 2012 Vicente Botet
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// adapted from the example given by Howard Hinnant in

#define BOOST_THREAD_VERSION 4

#include <iostream>
#include <boost/thread/scoped_thread.hpp>
#include <boost/thread/externally_locked_stream.hpp>
#include <boost/thread/sync_bounded_queue.hpp>

void producer(boost::externally_locked_stream<std::ostream> &mos, boost::sync_bounded_queue<int> & sbq)
{
  using namespace boost;
  try {
    for(int i=0; ;++i)
    {
      //sbq.push(i);
      sbq << i;
      mos << "push(" << i << ") "<< sbq.size()<<"\n";
      this_thread::sleep_for(chrono::milliseconds(200));
    }
  }
  catch(sync_queue_is_closed&)
  {
    mos << "closed !!!\n";
  }
  catch(...)
  {
    mos << "exception !!!\n";
  }
}

void consumer(boost::externally_locked_stream<std::ostream> &mos, boost::sync_bounded_queue<int> & sbq)
{
  using namespace boost;
  try {
    for(int i=0; ;++i)
    {
      int r;
      //sbq.pull(r);
      sbq >> r;
      mos << i << " pull(" << r << ") "<< sbq.size()<<"\n";
      this_thread::sleep_for(chrono::milliseconds(250));
    }
  }
  catch(sync_queue_is_closed&)
  {
    mos << "closed !!!\n";
  }
  catch(...)
  {
    mos << "exception !!!\n";
  }
}
void consumer2(boost::externally_locked_stream<std::ostream> &mos, boost::sync_bounded_queue<int> & sbq)
{
  using namespace boost;
  try {
    bool closed=false;
    for(int i=0; ;++i)
    {
      int r;
      sbq.pull(r, closed);
      if (closed) break;
      mos << i << " pull(" << r << ")\n";
      this_thread::sleep_for(chrono::milliseconds(250));
    }
  }
  catch(...)
  {
    mos << "exception !!!\n";
  }
}
//void consumer3(boost::externally_locked_stream<std::ostream> &mos, boost::sync_bounded_queue<int> & sbq)
//{
//  using namespace boost;
//  bool closed=false;
//  try {
//    for(int i=0; ;++i)
//    {
//      int r;
//      queue_op_status res = sbq.wait_and_pull(r);
//      if (res==queue_op_status::closed) break;
//      mos << i << " wait_and_pull(" << r << ")\n";
//      this_thread::sleep_for(chrono::milliseconds(250));
//    }
//  }
//  catch(...)
//  {
//    mos << "exception !!!\n";
//  }
//}

int main()
{
  using namespace boost;

  recursive_mutex terminal_mutex;

  externally_locked_stream<std::ostream> mcerr(std::cerr, terminal_mutex);
  externally_locked_stream<std::ostream> mcout(std::cout, terminal_mutex);
  externally_locked_stream<std::istream> mcin(std::cin, terminal_mutex);

  sync_bounded_queue<int> sbq(10);

  {
    mcout << "begin of main" << std::endl;
    scoped_thread<> t11(thread(producer, boost::ref(mcerr), boost::ref(sbq)));
    scoped_thread<> t12(thread(producer, boost::ref(mcerr), boost::ref(sbq)));
    scoped_thread<> t2(thread(consumer, boost::ref(mcout), boost::ref(sbq)));

    this_thread::sleep_for(chrono::seconds(1));

    sbq.close();
    mcout << "closed()" << std::endl;

  } // all threads joined here.
  mcout << "end of main" << std::endl;
  return 0;
}

