#pragma once

#include "boost/asio/ssl.hpp"
#include "ccapi_cpp/ccapi_logger.h"
#include <atomic>
#include <cstdint>
#include <string>

namespace ccapi {

/**
 * Defines the service that the service depends on.
 */
class ServiceContext {
 public:
  enum class NetworkStack {
    ASIO,
    DPDK
  };
  // Snapshot returned by getNetworkMetricsSnapshot(); not used for live storage.
  struct NetworkMetrics {
    std::string activeNetworkStack{"ASIO"};
    bool dpdkRequested{};
    bool dpdkActive{};
    int64_t dpdkFallbackCount{};
    int64_t wsBytesSent{};
    int64_t wsBytesReceived{};
    int64_t httpBytesSent{};
    int64_t httpBytesReceived{};
    int64_t wsConnectCount{};
    int64_t wsConnectFailureCount{};
    int64_t httpRequestCount{};
    int64_t httpRequestFailureCount{};
  };
  typedef boost::asio::io_context IoContext;
  typedef boost::asio::io_context* IoContextPtr;
  typedef boost::asio::executor_work_guard<boost::asio::io_context::executor_type> ExecutorWorkGuard;
  typedef ExecutorWorkGuard* ExecutorWorkGuardPtr;
  typedef boost::asio::ssl::context SslContext;
  typedef SslContext* SslContextPtr;

  ServiceContext() {
    this->ioContextPtr = new boost::asio::io_context();
    this->useInternalIoContextPtr = true;
    this->executorWorkGuardPtr = new ExecutorWorkGuard(this->ioContextPtr->get_executor());
    this->sslContextPtr = new SslContext(SslContext::tls_client);
    this->useInternalSslContextPtr = true;
    // this->sslContextPtr->set_options(SslContext::default_workarounds | SslContext::no_sslv2 | SslContext::no_sslv3 | SslContext::single_dh_use);
    this->sslContextPtr->set_verify_mode(boost::asio::ssl::verify_none);
    // TODO(cryptochassis): verify ssl certificate to strengthen security
    // https://github.com/boostorg/asio/blob/develop/example/cpp03/ssl/client.cpp
  }
#ifndef SWIG
  ServiceContext(IoContextPtr ioContextPtr) {
    this->ioContextPtr = ioContextPtr;
    this->executorWorkGuardPtr = new ExecutorWorkGuard(this->ioContextPtr->get_executor());
    this->sslContextPtr = new SslContext(SslContext::tls_client);
    this->useInternalSslContextPtr = true;
    this->sslContextPtr->set_verify_mode(boost::asio::ssl::verify_none);
  }

  ServiceContext(SslContextPtr sslContextPtr) {
    this->ioContextPtr = new boost::asio::io_context();
    this->useInternalIoContextPtr = true;
    this->executorWorkGuardPtr = new ExecutorWorkGuard(this->ioContextPtr->get_executor());
    this->sslContextPtr = sslContextPtr;
    this->sslContextPtr->set_verify_mode(boost::asio::ssl::verify_none);
  }

  ServiceContext(IoContextPtr ioContextPtr, SslContextPtr sslContextPtr) {
    this->ioContextPtr = ioContextPtr;
    this->executorWorkGuardPtr = new ExecutorWorkGuard(this->ioContextPtr->get_executor());
    this->sslContextPtr = sslContextPtr;
    this->sslContextPtr->set_verify_mode(boost::asio::ssl::verify_none);
  }
#endif
  ServiceContext(const ServiceContext&) = delete;
  ServiceContext& operator=(const ServiceContext&) = delete;

  virtual ~ServiceContext() {
    delete this->executorWorkGuardPtr;
    if (this->useInternalIoContextPtr) {
      delete this->ioContextPtr;
    }
    if (this->useInternalSslContextPtr) {
      delete this->sslContextPtr;
    }
  }

  void start() {
    if (this->useInternalIoContextPtr) {
      std::thread thread([this]() {
        CCAPI_LOGGER_INFO("about to start asio io_context run loop");
        this->ioContextPtr->run();
        CCAPI_LOGGER_INFO("just exited asio io_context run loop");
      });
      this->thread = std::move(thread);
    }
  }

  // Called from Session::start() before the I/O thread is running; no concurrency at this point.
  void initializeNetworkStack(const std::string& configuredNetworkStack) {
    this->networkStack = NetworkStack::ASIO;
    this->networkMetricsDpdkActive.store(false, std::memory_order_relaxed);
    bool isDpdk = (configuredNetworkStack == "DPDK");
    this->networkMetricsDpdkRequested.store(isDpdk, std::memory_order_relaxed);
    if (isDpdk) {
      // NOTE: this repository currently uses Boost.Asio/Beast sockets. DPDK integration requires a dedicated userspace NIC path.
      // We intentionally keep API compatibility by falling back to ASIO when DPDK is not compiled in.
      // Increment only on the first DPDK request (not on every session restart).
      if (this->networkMetricsDpdkFallbackCount.load(std::memory_order_relaxed) == 0) {
        this->networkMetricsDpdkFallbackCount.fetch_add(1, std::memory_order_relaxed);
      }
      CCAPI_LOGGER_WARN("sessionOptions.networkStack=DPDK requested but DPDK backend is not enabled in this build; falling back to ASIO");
    }
  }
  void setNetworkMetricsEnabled(bool enabled) {
    this->networkMetricsEnabled.store(enabled, std::memory_order_relaxed);
  }

  void addWsBytesSent(std::size_t n) {
    if (this->networkMetricsEnabled.load(std::memory_order_relaxed)) {
      this->networkMetricsWsBytesSent.fetch_add(static_cast<int64_t>(n), std::memory_order_relaxed);
    }
  }
  void addWsBytesReceived(std::size_t n) {
    if (this->networkMetricsEnabled.load(std::memory_order_relaxed)) {
      this->networkMetricsWsBytesReceived.fetch_add(static_cast<int64_t>(n), std::memory_order_relaxed);
    }
  }
  void addHttpBytesSent(std::size_t n) {
    if (this->networkMetricsEnabled.load(std::memory_order_relaxed)) {
      this->networkMetricsHttpBytesSent.fetch_add(static_cast<int64_t>(n), std::memory_order_relaxed);
    }
  }
  void addHttpBytesReceived(std::size_t n) {
    if (this->networkMetricsEnabled.load(std::memory_order_relaxed)) {
      this->networkMetricsHttpBytesReceived.fetch_add(static_cast<int64_t>(n), std::memory_order_relaxed);
    }
  }
  void incrementWsConnectCount() {
    if (this->networkMetricsEnabled.load(std::memory_order_relaxed)) {
      this->networkMetricsWsConnectCount.fetch_add(1, std::memory_order_relaxed);
    }
  }
  void incrementWsConnectFailureCount() {
    if (this->networkMetricsEnabled.load(std::memory_order_relaxed)) {
      this->networkMetricsWsConnectFailureCount.fetch_add(1, std::memory_order_relaxed);
    }
  }
  void incrementHttpRequestCount() {
    if (this->networkMetricsEnabled.load(std::memory_order_relaxed)) {
      this->networkMetricsHttpRequestCount.fetch_add(1, std::memory_order_relaxed);
    }
  }
  void incrementHttpRequestFailureCount() {
    if (this->networkMetricsEnabled.load(std::memory_order_relaxed)) {
      this->networkMetricsHttpRequestFailureCount.fetch_add(1, std::memory_order_relaxed);
    }
  }

  // Lock-free snapshot: individual atomic loads; each counter is independently consistent.
  // Sufficient for monitoring; does not provide a globally consistent point-in-time view.
  NetworkMetrics getNetworkMetricsSnapshot() const {
    NetworkMetrics m;
    m.activeNetworkStack = this->networkStack == NetworkStack::DPDK ? "DPDK" : "ASIO";
    m.dpdkRequested = this->networkMetricsDpdkRequested.load(std::memory_order_relaxed);
    m.dpdkActive = this->networkMetricsDpdkActive.load(std::memory_order_relaxed);
    m.dpdkFallbackCount = this->networkMetricsDpdkFallbackCount.load(std::memory_order_relaxed);
    m.wsBytesSent = this->networkMetricsWsBytesSent.load(std::memory_order_relaxed);
    m.wsBytesReceived = this->networkMetricsWsBytesReceived.load(std::memory_order_relaxed);
    m.httpBytesSent = this->networkMetricsHttpBytesSent.load(std::memory_order_relaxed);
    m.httpBytesReceived = this->networkMetricsHttpBytesReceived.load(std::memory_order_relaxed);
    m.wsConnectCount = this->networkMetricsWsConnectCount.load(std::memory_order_relaxed);
    m.wsConnectFailureCount = this->networkMetricsWsConnectFailureCount.load(std::memory_order_relaxed);
    m.httpRequestCount = this->networkMetricsHttpRequestCount.load(std::memory_order_relaxed);
    m.httpRequestFailureCount = this->networkMetricsHttpRequestFailureCount.load(std::memory_order_relaxed);
    return m;
  }

  void stop() {
    this->executorWorkGuardPtr->reset();
    if (this->useInternalIoContextPtr) {
      this->ioContextPtr->stop();
      this->thread.join();
    }
  }

  IoContextPtr ioContextPtr{nullptr};
  bool useInternalIoContextPtr{};
  ExecutorWorkGuardPtr executorWorkGuardPtr{nullptr};
  SslContextPtr sslContextPtr{nullptr};
  bool useInternalSslContextPtr{};
  std::thread thread;
  NetworkStack networkStack{NetworkStack::ASIO};

  // Guard flag: atomic bool, read on every I/O callback (hot path).
  std::atomic<bool> networkMetricsEnabled{false};

  // Atomic counters: written on every send/receive (hot path); no mutex needed.
  std::atomic<bool>    networkMetricsDpdkRequested{false};
  std::atomic<bool>    networkMetricsDpdkActive{false};
  std::atomic<int64_t> networkMetricsDpdkFallbackCount{0};
  std::atomic<int64_t> networkMetricsWsBytesSent{0};
  std::atomic<int64_t> networkMetricsWsBytesReceived{0};
  std::atomic<int64_t> networkMetricsHttpBytesSent{0};
  std::atomic<int64_t> networkMetricsHttpBytesReceived{0};
  std::atomic<int64_t> networkMetricsWsConnectCount{0};
  std::atomic<int64_t> networkMetricsWsConnectFailureCount{0};
  std::atomic<int64_t> networkMetricsHttpRequestCount{0};
  std::atomic<int64_t> networkMetricsHttpRequestFailureCount{0};
};

} /* namespace ccapi */
