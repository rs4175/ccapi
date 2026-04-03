#pragma once

#include "boost/asio/ssl.hpp"
#include "ccapi_cpp/ccapi_logger.h"
#include <mutex>
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
  struct NetworkMetrics {
    std::string activeNetworkStack{"ASIO"};
    bool dpdkRequested{};
    bool dpdkActive{};
    long dpdkFallbackCount{};
    long wsBytesSent{};
    long wsBytesReceived{};
    long httpBytesSent{};
    long httpBytesReceived{};
    long wsConnectCount{};
    long wsConnectFailureCount{};
    long httpRequestCount{};
    long httpRequestFailureCount{};
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

  void initializeNetworkStack(const std::string& configuredNetworkStack) {
    std::lock_guard<std::mutex> lock(this->networkMetricsMutex);
    this->networkMetrics.dpdkRequested = configuredNetworkStack == "DPDK";
    if (configuredNetworkStack == "DPDK") {
      // NOTE: this repository currently uses Boost.Asio/Beast sockets. DPDK integration requires a dedicated userspace NIC path.
      // We intentionally keep API compatibility by falling back to ASIO when DPDK is not compiled in.
      this->networkStack = NetworkStack::ASIO;
      this->networkMetrics.dpdkActive = false;
      this->networkMetrics.dpdkFallbackCount += 1;
      this->networkMetrics.activeNetworkStack = "ASIO";
      CCAPI_LOGGER_WARN("sessionOptions.networkStack=DPDK requested but DPDK backend is not enabled in this build; falling back to ASIO");
    } else {
      this->networkStack = NetworkStack::ASIO;
      this->networkMetrics.dpdkActive = false;
      this->networkMetrics.activeNetworkStack = "ASIO";
    }
  }
  void setNetworkMetricsEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(this->networkMetricsMutex);
    this->networkMetricsEnabled = enabled;
  }

  void addWsBytesSent(std::size_t n) {
    if (!this->networkMetricsEnabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(this->networkMetricsMutex);
    this->networkMetrics.wsBytesSent += static_cast<long>(n);
  }
  void addWsBytesReceived(std::size_t n) {
    if (!this->networkMetricsEnabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(this->networkMetricsMutex);
    this->networkMetrics.wsBytesReceived += static_cast<long>(n);
  }
  void addHttpBytesSent(std::size_t n) {
    if (!this->networkMetricsEnabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(this->networkMetricsMutex);
    this->networkMetrics.httpBytesSent += static_cast<long>(n);
  }
  void addHttpBytesReceived(std::size_t n) {
    if (!this->networkMetricsEnabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(this->networkMetricsMutex);
    this->networkMetrics.httpBytesReceived += static_cast<long>(n);
  }
  void incrementWsConnectCount() {
    if (!this->networkMetricsEnabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(this->networkMetricsMutex);
    this->networkMetrics.wsConnectCount += 1;
  }
  void incrementWsConnectFailureCount() {
    if (!this->networkMetricsEnabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(this->networkMetricsMutex);
    this->networkMetrics.wsConnectFailureCount += 1;
  }
  void incrementHttpRequestCount() {
    if (!this->networkMetricsEnabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(this->networkMetricsMutex);
    this->networkMetrics.httpRequestCount += 1;
  }
  void incrementHttpRequestFailureCount() {
    if (!this->networkMetricsEnabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(this->networkMetricsMutex);
    this->networkMetrics.httpRequestFailureCount += 1;
  }
  NetworkMetrics getNetworkMetricsSnapshot() const {
    std::lock_guard<std::mutex> lock(this->networkMetricsMutex);
    return this->networkMetrics;
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
  bool networkMetricsEnabled{true};
  mutable std::mutex networkMetricsMutex;
  NetworkMetrics networkMetrics;
};

} /* namespace ccapi */
