#pragma once

#include "performance_monitor.hpp"
#include <string>
#include <sstream>
#include <iomanip>

namespace task_engine {

/**
 * Metrics exporter for various formats
 */
class MetricsExporter {
public:
    /**
     * Export metrics as JSON
     */
    static std::string to_json(const PerformanceMonitor::MetricsSnapshot& metrics) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "{\n";
        oss << "  \"tasks_submitted\": " << metrics.tasks_submitted << ",\n";
        oss << "  \"tasks_completed\": " << metrics.tasks_completed << ",\n";
        oss << "  \"tasks_failed\": " << metrics.tasks_failed << ",\n";
        oss << "  \"tasks_retried\": " << metrics.tasks_retried << ",\n";
        oss << "  \"tasks_cancelled\": " << metrics.tasks_cancelled << ",\n";
        oss << "  \"total_execution_time_us\": " << metrics.total_execution_time_us << ",\n";
        oss << "  \"max_execution_time_us\": " << metrics.max_execution_time_us << ",\n";
        oss << "  \"min_execution_time_us\": " << metrics.min_execution_time_us << ",\n";
        oss << "  \"total_queue_wait_time_us\": " << metrics.total_queue_wait_time_us << ",\n";
        oss << "  \"inflight_tasks\": " << metrics.inflight_tasks << ",\n";
        oss << "  \"peak_inflight\": " << metrics.peak_inflight << "\n";
        oss << "}";
        return oss.str();
    }

    /**
     * Export metrics in Prometheus format
     */
    static std::string to_prometheus(const PerformanceMonitor::MetricsSnapshot& metrics,
                                     const std::string& prefix = "nexustask") {
        std::ostringstream oss;
        oss << "# HELP " << prefix << "_tasks_submitted Total number of tasks submitted\n";
        oss << "# TYPE " << prefix << "_tasks_submitted counter\n";
        oss << prefix << "_tasks_submitted " << metrics.tasks_submitted << "\n\n";
        
        oss << "# HELP " << prefix << "_tasks_completed Total number of tasks completed\n";
        oss << "# TYPE " << prefix << "_tasks_completed counter\n";
        oss << prefix << "_tasks_completed " << metrics.tasks_completed << "\n\n";
        
        oss << "# HELP " << prefix << "_tasks_failed Total number of tasks failed\n";
        oss << "# TYPE " << prefix << "_tasks_failed counter\n";
        oss << prefix << "_tasks_failed " << metrics.tasks_failed << "\n\n";
        
        oss << "# HELP " << prefix << "_inflight_tasks Current number of inflight tasks\n";
        oss << "# TYPE " << prefix << "_inflight_tasks gauge\n";
        oss << prefix << "_inflight_tasks " << metrics.inflight_tasks << "\n\n";
        
        oss << "# HELP " << prefix << "_peak_inflight Peak number of inflight tasks\n";
        oss << "# TYPE " << prefix << "_peak_inflight gauge\n";
        oss << prefix << "_peak_inflight " << metrics.peak_inflight << "\n";
        
        return oss.str();
    }

    /**
     * Export metrics as CSV
     */
    static std::string to_csv(const PerformanceMonitor::MetricsSnapshot& metrics) {
        std::ostringstream oss;
        oss << "metric,value\n";
        oss << "tasks_submitted," << metrics.tasks_submitted << "\n";
        oss << "tasks_completed," << metrics.tasks_completed << "\n";
        oss << "tasks_failed," << metrics.tasks_failed << "\n";
        oss << "tasks_retried," << metrics.tasks_retried << "\n";
        oss << "tasks_cancelled," << metrics.tasks_cancelled << "\n";
        oss << "total_execution_time_us," << metrics.total_execution_time_us << "\n";
        oss << "max_execution_time_us," << metrics.max_execution_time_us << "\n";
        oss << "min_execution_time_us," << metrics.min_execution_time_us << "\n";
        oss << "total_queue_wait_time_us," << metrics.total_queue_wait_time_us << "\n";
        oss << "inflight_tasks," << metrics.inflight_tasks << "\n";
        oss << "peak_inflight," << metrics.peak_inflight << "\n";
        return oss.str();
    }
};

} // namespace task_engine

