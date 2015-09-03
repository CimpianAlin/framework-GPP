/*
 * This file is protected by Copyright. Please refer to the COPYRIGHT file
 * distributed with this source distribution.
 *
 * This file is part of REDHAWK GPP.
 *
 * REDHAWK GPP is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * REDHAWK GPP is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */
#ifndef GPP_IMPL_H
#define GPP_IMPL_H

#include "GPP_base.h"
#include <boost/shared_ptr.hpp>
#include <boost/filesystem/path.hpp>
#include <sys/resource.h>

#include "utils/Updateable.h"
#include "reports/ThresholdMonitor.h"
#include "states/State.h"
#include "statistics/Statistics.h"
#include "statistics/CpuUsageStats.h"
#include "reports/SystemMonitorReporting.h"
#include "reports/CpuThresholdMonitor.h"
#include "NicFacade.h"
#include "ossie/Events.h"

class ThresholdMonitor;
class NicFacade;


#if BOOST_FILESYSTEM_VERSION < 3
#define BOOST_PATH_STRING(x) (x)
#else
#define BOOST_PATH_STRING(x) (x).string()
#endif


class GPP_i : public GPP_base
{
    ENABLE_LOGGING
    public:
        GPP_i(char *devMgr_ior, char *id, char *lbl, char *sftwrPrfl);
        GPP_i(char *devMgr_ior, char *id, char *lbl, char *sftwrPrfl, char *compDev);
        GPP_i(char *devMgr_ior, char *id, char *lbl, char *sftwrPrfl, CF::Properties capacities);
        GPP_i(char *devMgr_ior, char *id, char *lbl, char *sftwrPrfl, CF::Properties capacities, char *compDev);
        ~GPP_i();

        int serviceFunction();
        void initializeNetworkMonitor();
        void initializeResourceMonitors();
        void addThresholdMonitor( ThresholdMonitor* threshold_monitor );
        void send_threshold_event(const threshold_event_struct& message);
        
        void initialize() throw (CF::LifeCycle::InitializeError, CORBA::SystemException);
        bool allocate_loadCapacity(const double &value);
        void deallocate_loadCapacity(const double &value);
        bool allocate_diskCapacity(const double &value);
        void deallocate_diskCapacity(const double &value);
        bool allocate_memCapacity(const int64_t &value);
        void deallocate_memCapacity(const int64_t &value);

        CF::ExecutableDevice::ProcessID_Type execute (const char* name, const CF::Properties& options, const CF::Properties& parameters)
            throw (CORBA::SystemException, CF::Device::InvalidState, CF::ExecutableDevice::InvalidFunction, 
                   CF::ExecutableDevice::InvalidParameters, CF::ExecutableDevice::InvalidOptions, 
                   CF::InvalidFileName, CF::ExecutableDevice::ExecuteFail);
        void terminate (CF::ExecutableDevice::ProcessID_Type processId)
            throw (CORBA::SystemException, CF::ExecutableDevice::InvalidProcess, CF::Device::InvalidState);
        void updateThresholdMonitors();
        void sendChildNotification(const std::string &comp_id, const std::string &app_id);
        bool allocateCapacity_nic_allocation(const nic_allocation_struct &value);
        void deallocateCapacity_nic_allocation(const nic_allocation_struct &value);
        void deallocateCapacity (const CF::Properties& capacities) throw (CF::Device::InvalidState, CF::Device::InvalidCapacity, CORBA::SystemException);
        CORBA::Boolean allocateCapacity (const CF::Properties& capacities) throw (CF::Device::InvalidState, CF::Device::InvalidCapacity, CF::Device::InsufficientCapacity, CORBA::SystemException);
        void releaseObject() throw (CORBA::SystemException, CF::LifeCycle::ReleaseError);

        int sigchld_handler( int sig );
    protected:
	struct component_description {
	  std::string appName;
	  std::string identifier;
	};

        struct LoadCapacity {
          float max;
          float measured;
          float allocated;
        };

        //
        // base execution unit for partitioning a host system
        //
        struct exec_socket : public Updateable {
          int                              id;
          CpuUsageStats::CpuList           cpus;
          CpuUsageStats                    stats;
          LoadCapacity                     load_capacity;              // future
          double                           idle_threshold;
          double                           idle_cap_mod;

        exec_socket() : idle_threshold(0.0), idle_cap_mod(0.0) {};

          void update() {
            stats.compute_statistics();
          };

          double get_idle_percent() const {
            return stats.get_idle_percent();
          }

          double get_idle_average() const {
            return stats.get_idle_average();
          }

          bool is_available() const {
            return stats.get_idle_percent() > idle_threshold ;
          };
        };

        typedef std::vector< exec_socket >      ExecPartitionList;


	friend bool operator==( const component_description &, 
				const component_description & );

        std::vector<int> getPids();
        component_description getComponentDescription(int pid);


         void  set_resource_affinity( const CF::Properties& options,
                                      const pid_t rsc_pid,
                                      const char  *rsc_name,
                                      const std::vector<int> &bl);           


        void process_ODM(const CORBA::Any &data);
        void updateUsageState();

	typedef boost::shared_ptr<ThresholdMonitor>           ThresholdMonitorPtr;
        typedef std::vector< uint32_t >                       CpuList;
        typedef std::vector< boost::shared_ptr<Updateable> >  UpdateableSequence;
        typedef std::vector<boost::shared_ptr<State> >        StateSequence;
        typedef std::vector<boost::shared_ptr<Statistics> >   StatisticsSequence;
        typedef std::vector<boost::shared_ptr<Reporting> >    ReportingSequence;
        typedef std::vector< ThresholdMonitorPtr >            MonitorSequence;
        typedef boost::shared_ptr<SystemMonitor>              SystemMonitorPtr;
        typedef std::map<int, component_description >         ProcessMap;
        typedef std::vector< component_description >          ProcessList;

        void addPid(int pid, std::string appName, std::string identifier);
        void removePid(int pid);
        void addReservation(const component_description &component);
        void removeReservation(const component_description &component);
        void tableReservation(const component_description &component);
        void restoreReservation(const component_description &component);
        void reservedChanged(const float *oldValue, const float *newValue);
        void update();
        //void sigchld_handler( int sig );

        ProcessList                                         reservations;
        ProcessList                                         tabled_reservations;
        ProcessMap                                          pids;
        boost::mutex                                        pidLock;

        NicFacadePtr                                        nic_facade;
        MonitorSequence                                     threshold_monitors;
        SystemMonitorPtr                                    system_monitor;
        ProcessLimitsPtr                                    process_limits;
        ExecPartitionList                                   execPartitions;
        
        UpdateableSequence                                  data_model;
        thresholds_struct                                   modified_thresholds;
        uint64_t                                            mem_free_units;
        uint64_t                                            mem_cap_units;
        float                                               idle_capacity_modifier;
        CpuList                                             wl_cpus;            // list of allowable cpus to run on .... empty == all, derived from affnity blacklist property and host machine
        CpuList                                             bl_cpus;            // list of blacklist cpus to avoid

        std::string                                         binary_location;    // path to this program.
        
        boost::posix_time::ptime                            time_mark;          // time marker for update
        redhawk::events::SubscriberPtr                      odm_consumer;       // interface that receives ODM_Channel events
        redhawk::events::ManagerPtr                         mymgr;              // interface to manage event channel access

 private:

        bool  _component_cleanup( const int pid );

        //
        // setup execution partitions for launching components
        // 
        int   _setupExecPartitions( const CpuList &blacklist );

        //
        // apply specific affinity settings to a pid
        //
        int   _apply_affinity(  const pid_t rsc_pid, 
                                const char *rsc_name,
                                const std::string &affinity_class,
                                const std::string &affinity_value,
                                const CpuList &bl_cpus );
        
        //
        // apply affinity settings for affinity struct property
        //
        int   _apply_affinity( const affinity_struct &affinity, const pid_t rsc_pid, const char *rsc_name  );

        //
        // get the next available partition to use for luanching resources
        //
        int   _get_deploy_on_partition();

        //
        // Check if execution partition for a NIC interface has enough processing capacity 
        //
        bool _check_exec_partition( const std::string &iface );

        //
        // Callback when affinity processing structure is changed
        //
        void _affinity_changed(const affinity_struct *ov, const affinity_struct *nv );

        //
        // Determine list of CPUs that are monitored
        //
        void _set_processor_monitor_list( const CpuList &cl );

        //
        // Common method called by all CTORs
        //
        void _init();
        
        //
        // check file and thread limits for the process and system
        //
        bool _check_limits( const thresholds_struct &threshold);
        std::string user_id;
        int limit_check_count;

        ossie::ProcessThread                                _signalThread;

};

#endif // GPP_IMPL_H
