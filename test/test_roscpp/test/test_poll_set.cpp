/*
 * Copyright (c) 2008, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Author: Josh Faust */

/*
 * Test version macros
 */

#include <gtest/gtest.h>
#include "ros/poll_set.h"
//#include <sys/socket.h>

#include <fcntl.h>

#include <boost/bind.hpp>
#include <boost/thread.hpp>

using namespace ros;

int set_non_blocking1(int &socket) {
#ifdef WIN32
	u_long non_blocking = 1;
	if (ioctlsocket(socket, FIONBIO, &non_blocking) != 0)
	{
		return WSAGetLastError();
	}
#else
	if (fcntl(socket, F_SETFL, O_NONBLOCK) == -1)
	{
		return errno;
	}
#endif
	return 0;
}

int create_signal_pair1(int signal_pair[2]) {
#ifdef WIN32 // use a socket pair
	signal_pair[0] = INVALID_SOCKET;
	signal_pair[1] = INVALID_SOCKET;

	union {
		struct sockaddr_in inaddr;
		struct sockaddr addr;
	} a;
	socklen_t addrlen = sizeof(a.inaddr);

	/*********************
	** Listen Socket
	**********************/
	socket_fd_t listen_socket = INVALID_SOCKET;
	listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_socket == INVALID_SOCKET) {
		return -1;
	}

	// allow it to be bound to an address already in use - do we actually need this?
	int reuse = 1;
	if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, (socklen_t) sizeof(reuse)) == SOCKET_ERROR) {
		::closesocket(listen_socket);
		return -1;
	}

	memset(&a, 0, sizeof(a));
	a.inaddr.sin_family = AF_INET;
	a.inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	// For TCP/IP, if the port is specified as zero, the service provider assigns
	// a unique port to the application from the dynamic client port range.
	a.inaddr.sin_port = 0;

	if (bind(listen_socket, &a.addr, sizeof(a.inaddr)) == SOCKET_ERROR) {
		::closesocket(listen_socket);
		return -1;
	}
	// we need this below because the system auto filled in some entries, e.g. port #
	if (getsockname(listen_socket, &a.addr, &addrlen) == SOCKET_ERROR) {
		::closesocket(listen_socket);
		return -1;
	}
	// max 1 connection permitted
	if (listen(listen_socket, 1) == SOCKET_ERROR) {
		::closesocket(listen_socket);
		return -1;
	}

	/*********************
	** Connection
	**********************/
	// do we need io overlapping?
	// DWORD flags = (make_overlapped ? WSA_FLAG_OVERLAPPED : 0);
	DWORD overlapped_flag = 0;
	signal_pair[0] = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, overlapped_flag);
	if (signal_pair[0] == INVALID_SOCKET) {
		::closesocket(listen_socket);
		::closesocket(signal_pair[0]);
		return -1;
	}
	// reusing the information from above to connect to the listener
	if (connect(signal_pair[0], &a.addr, sizeof(a.inaddr)) == SOCKET_ERROR) {
		::closesocket(listen_socket);
		::closesocket(signal_pair[0]);
		return -1;
	}
	/*********************
	** Accept
	**********************/
	signal_pair[1] = accept(listen_socket, NULL, NULL);
	if (signal_pair[1] == INVALID_SOCKET) {
		::closesocket(listen_socket);
		::closesocket(signal_pair[0]);
		::closesocket(signal_pair[1]);
		return -1;
	}
	/*********************
	** Nonblocking
	**********************/
	// should we do this or should we set io overlapping?
	if ((set_non_blocking1(signal_pair[0]) != 0) || (set_non_blocking1(signal_pair[1]) != 0)) {
		::closesocket(listen_socket);
		::closesocket(signal_pair[0]);
		::closesocket(signal_pair[1]);
		return -1;
	}
	/*********************
	** Cleanup
	**********************/
	::closesocket(listen_socket);  // the listener has done its job.
	return 0;
#else // use a pipe pair
	// initialize
	signal_pair[0] = -1;
	signal_pair[1] = -1;

	if (pipe(signal_pair) != 0) {
		ROS_FATAL("pipe() failed");
		return -1;
	}
	if (fcntl(signal_pair[0], F_SETFL, O_NONBLOCK) == -1) {
		ROS_FATAL("fcntl() failed");
		return -1;
	}
	if (fcntl(signal_pair[1], F_SETFL, O_NONBLOCK) == -1) {
		ROS_FATAL("fcntl() failed");
		return -1;
	}
	return 0;
#endif // create_pipe
}

class Poller : public testing::Test
{
public:
  Poller()
  {
  }

  ~Poller()
  {
    ::close(sockets_[0]);
    ::close(sockets_[1]);
  }

  void waitThenSignal()
  {
    boost::this_thread::sleep(boost::posix_time::microseconds(100000));

    poll_set_.signal();
  }

protected:

  virtual void SetUp()
  {
#ifndef _WIN32
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets_) != 0)
    {
      FAIL();
    }
    if(fcntl(sockets_[0], F_SETFL, O_NONBLOCK) == -1)
    {
      FAIL();
    }
    if(fcntl(sockets_[1], F_SETFL, O_NONBLOCK) == -1)
    {
      FAIL();
    }
#else
    if (create_signal_pair1(sockets_))
    {
      FAIL();
    }
#endif
  }

  PollSet poll_set_;

  int sockets_[2];


};

class SocketHelper
{
public:
  SocketHelper(int sock)
  : bytes_read_(0)
  , bytes_written_(0)
  , pollouts_received_(0)
  , socket_(sock)
  {}

  void processEvents(int events)
  {
    if (events & POLLIN)
    {
      char b;
      while(read(socket_, &b, 1) > 0)
      {
        ++bytes_read_;
      };
    }

    if (events & POLLOUT)
    {
      ++pollouts_received_;

      write();
    }
  }

  void write()
  {
    char b = 0;
    if (::write(socket_, &b, 1) > 0)
    {
      ++bytes_written_;
    }
  }

  int bytes_read_;
  int bytes_written_;
  int pollouts_received_;
  int socket_;
};

TEST_F(Poller, read)
{
  SocketHelper sh(sockets_[0]);
  ASSERT_TRUE(poll_set_.addSocket(sh.socket_, boost::bind(&SocketHelper::processEvents, &sh, _1)));

  char b = 0;

  write(sockets_[1], &b, 1);
  poll_set_.update(1);

  ASSERT_EQ(sh.bytes_read_, 0);

  ASSERT_TRUE(poll_set_.addEvents(sh.socket_, POLLIN));
  poll_set_.update(1);
  ASSERT_EQ(sh.bytes_read_, 1);

  write(sockets_[1], &b, 1);
  poll_set_.update(1);
  ASSERT_EQ(sh.bytes_read_, 2);

  ASSERT_TRUE(poll_set_.delEvents(sh.socket_, POLLIN));
  write(sockets_[1], &b, 1);
  poll_set_.update(1);
  ASSERT_EQ(sh.bytes_read_, 2);

  ASSERT_TRUE(poll_set_.addEvents(sh.socket_, POLLIN));
  poll_set_.update(1);
  ASSERT_EQ(sh.bytes_read_, 3);

  ASSERT_TRUE(poll_set_.delSocket(sockets_[0]));
  poll_set_.update(1);
  ASSERT_EQ(sh.bytes_read_, 3);
}

TEST_F(Poller, write)
{
  SocketHelper sh(sockets_[0]);
  ASSERT_TRUE(poll_set_.addSocket(sh.socket_, boost::bind(&SocketHelper::processEvents, &sh, _1)));
  ASSERT_TRUE(poll_set_.addEvents(sh.socket_, POLLOUT));

  poll_set_.update(1);

  ASSERT_EQ(sh.pollouts_received_, 1);
  ASSERT_EQ(sh.bytes_written_, 1);

  ASSERT_TRUE(poll_set_.delEvents(sh.socket_, POLLOUT));
  poll_set_.update(1);
  ASSERT_EQ(sh.pollouts_received_, 1);
  ASSERT_EQ(sh.bytes_written_, 1);
}

TEST_F(Poller, readAndWrite)
{
  SocketHelper sh1(sockets_[0]);
  SocketHelper sh2(sockets_[1]);
  ASSERT_TRUE(poll_set_.addSocket(sh1.socket_, boost::bind(&SocketHelper::processEvents, &sh1, _1)));
  ASSERT_TRUE(poll_set_.addSocket(sh2.socket_, boost::bind(&SocketHelper::processEvents, &sh2, _1)));

  ASSERT_TRUE(poll_set_.addEvents(sh1.socket_, POLLIN));
  ASSERT_TRUE(poll_set_.addEvents(sh2.socket_, POLLIN));

  sh1.write();
  sh2.write();

  ASSERT_EQ(sh1.bytes_written_, 1);
  ASSERT_EQ(sh2.bytes_written_, 1);

  poll_set_.update(1);

  ASSERT_EQ(sh1.bytes_read_, 1);
  ASSERT_EQ(sh2.bytes_read_, 1);

  ASSERT_TRUE(poll_set_.addEvents(sh1.socket_, POLLOUT));
  ASSERT_TRUE(poll_set_.addEvents(sh2.socket_, POLLOUT));

  poll_set_.update(1);

  ASSERT_EQ(sh1.bytes_written_, 2);
  ASSERT_EQ(sh2.bytes_written_, 2);

  ASSERT_TRUE(poll_set_.delEvents(sh1.socket_, POLLOUT));
  ASSERT_TRUE(poll_set_.delEvents(sh2.socket_, POLLOUT));

  poll_set_.update(1);

  ASSERT_EQ(sh1.bytes_read_, 2);
  ASSERT_EQ(sh2.bytes_read_, 2);
}

TEST_F(Poller, multiAddDel)
{
  SocketHelper sh(sockets_[0]);
  ASSERT_TRUE(poll_set_.addSocket(sh.socket_, boost::bind(&SocketHelper::processEvents, &sh, _1)));
  ASSERT_FALSE(poll_set_.addSocket(sh.socket_, boost::bind(&SocketHelper::processEvents, &sh, _1)));

  ASSERT_TRUE(poll_set_.addEvents(sh.socket_, 0));
  ASSERT_FALSE(poll_set_.addEvents(sh.socket_ + 1, 0));

  ASSERT_TRUE(poll_set_.delEvents(sh.socket_, 0));
  ASSERT_FALSE(poll_set_.delEvents(sh.socket_ + 1, 0));

  ASSERT_FALSE(poll_set_.delSocket(sh.socket_ + 1));
  ASSERT_TRUE(poll_set_.delSocket(sh.socket_));
}

void addThread(PollSet* ps, SocketHelper* sh, boost::barrier* barrier)
{
  barrier->wait();

  ps->addSocket(sh->socket_, boost::bind(&SocketHelper::processEvents, sh, _1));
  ps->addEvents(sh->socket_, POLLIN);
  ps->addEvents(sh->socket_, POLLOUT);
}

void delThread(PollSet* ps, SocketHelper* sh, boost::barrier* barrier)
{
  barrier->wait();

  ps->delEvents(sh->socket_, POLLIN);
  ps->delEvents(sh->socket_, POLLOUT);
  ps->delSocket(sh->socket_);
}

/**
 * This test has been disabled. The underlying logic which it tests has three
 * different implementations (poll, epoll, Windows), and development of the epoll
 * version exposed that the test was validating a buggy aspect of the original
 * poll version. To reenable this test, the poll version and the test would both
 * have to be updated.
 *
 * For more discussion, see: https://github.com/ros/ros_comm/pull/1217
 */
TEST_F(Poller, DISABLED_addDelMultiThread)
{
  for (int i = 0; i < 100; ++i)
  {
    SocketHelper sh1(sockets_[0]);
    SocketHelper sh2(sockets_[1]);

    const int thread_count = 100;

    {
      boost::barrier barrier(thread_count + 1);

      boost::thread_group tg;
      for (int i = 0; i < thread_count/2; ++i)
      {
        tg.create_thread(boost::bind(addThread, &poll_set_, &sh1, &barrier));
        tg.create_thread(boost::bind(addThread, &poll_set_, &sh2, &barrier));
      }

      barrier.wait();

      tg.join_all();

      poll_set_.update(1);

      ASSERT_TRUE(sh1.bytes_read_ == 0 || sh1.bytes_read_ == 1);
      ASSERT_TRUE(sh2.bytes_read_ == 0 || sh2.bytes_read_ == 1);
      ASSERT_EQ(sh1.bytes_written_, 1);
      ASSERT_EQ(sh2.bytes_written_, 1);

      poll_set_.update(1);

      ASSERT_TRUE(sh1.bytes_read_ == 1 || sh1.bytes_read_ == 2);
      ASSERT_TRUE(sh2.bytes_read_ == 1 || sh2.bytes_read_ == 2);
      ASSERT_EQ(sh1.bytes_written_, 2);
      ASSERT_EQ(sh2.bytes_written_, 2);
    }

    {
      boost::barrier barrier(thread_count + 1);

      boost::thread_group tg;
      for (int i = 0; i < thread_count/2; ++i)
      {
        tg.create_thread(boost::bind(delThread, &poll_set_, &sh1, &barrier));
        tg.create_thread(boost::bind(delThread, &poll_set_, &sh2, &barrier));
      }

      barrier.wait();

      tg.join_all();

      poll_set_.update(1);

      ASSERT_TRUE(sh1.bytes_read_ == 1 || sh1.bytes_read_ == 2);
      ASSERT_TRUE(sh2.bytes_read_ == 1 || sh2.bytes_read_ == 2);
      ASSERT_EQ(sh1.bytes_written_, 2);
      ASSERT_EQ(sh2.bytes_written_, 2);
    }
  }
}

void addDelManyTimesThread(PollSet* ps, SocketHelper* sh1, SocketHelper* sh2, boost::barrier* barrier, int count, volatile bool* done)
{
  *done = false;

  barrier->wait();

  for (int i = 0; i < count; ++i)
  {
    ps->addSocket(sh1->socket_, boost::bind(&SocketHelper::processEvents, sh1, _1));
    ps->addEvents(sh1->socket_, POLLIN);
    ps->addEvents(sh1->socket_, POLLOUT);

    ps->addSocket(sh2->socket_, boost::bind(&SocketHelper::processEvents, sh2, _1));
    ps->addEvents(sh2->socket_, POLLIN);
    ps->addEvents(sh2->socket_, POLLOUT);

    boost::this_thread::sleep(boost::posix_time::microseconds(100));

    ps->delEvents(sh1->socket_, POLLIN);
    ps->delEvents(sh1->socket_, POLLOUT);
    ps->delSocket(sh1->socket_);

    ps->delEvents(sh2->socket_, POLLIN);
    ps->delEvents(sh2->socket_, POLLOUT);
    ps->delSocket(sh2->socket_);
  }

  *done = true;
}

TEST_F(Poller, updateWhileAddDel)
{
  SocketHelper sh1(sockets_[0]);
  SocketHelper sh2(sockets_[1]);

  boost::barrier barrier(2);
  volatile bool done = false;
  const int count = 1000;

  boost::thread t(boost::bind(addDelManyTimesThread, &poll_set_, &sh1, &sh2, &barrier, count, &done));

  barrier.wait();

  while (!done)
  {
    poll_set_.update(1);
  }

  ASSERT_TRUE(sh1.bytes_read_ > 0);
  ASSERT_TRUE(sh1.bytes_written_ > 0);
  ASSERT_TRUE(sh2.bytes_read_ > 0);
  ASSERT_TRUE(sh2.bytes_written_ > 0);
}

TEST_F(Poller, signal)
{
  // first one clears out any calls to signal() caused by construction
  poll_set_.update(0);

  boost::thread t(boost::bind(&Poller::waitThenSignal, this));
  poll_set_.update(-1);

  // wait for poll_set_.signal_mutex_ to be unlocked after invoking signal()
  boost::this_thread::sleep(boost::posix_time::microseconds(50000));
}


int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);

#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);
#endif

  return RUN_ALL_TESTS();
}

