/**************************************************************************

    This is the device code. This file contains the child class where
    custom functionality can be added to the device. Custom
    functionality to the base class can be extended here. Access to
    the ports can also be done from this class

**************************************************************************/

#include "GPP.h"
#include "boost/filesystem/path.hpp"
#include "utils/ReferenceWrapper.h"
#include <linux/limits.h>
#include "dirent.h"
#include <sys/wait.h>

PREPARE_LOGGING(GPP_i)

extern GPP_i *devicePtr;

static void sigchld_handler(int sig)
{
    int status, saved_errno;
    pid_t child_pid;
    
    saved_errno = errno;
    
    while( (child_pid = waitpid(-1, &status, WNOHANG)) > 0 )
    {
        try {
            component_description_struct retval;
            retval = devicePtr->getComponentDescription(child_pid);
            devicePtr->sendChildNotification(devicePtr->_identifier, retval.identifier, retval.appName);
            break;
        } catch ( ... ) {
        }
        /*if( WIFEXITED(status) && WEXITSTATUS(status) )
            printf("Child %ld exited with status %d\n", (long) child_pid, WEXITSTATUS(status) );
        else if( WIFSIGNALED(status) )
            printf("Child %ld killed by signal %d\n", (long) child_pid, WTERMSIG(status) );
        else if( WIFSTOPPED(status) )
            printf("Child %ld stopped by signal %d\n", (long) child_pid, WSTOPSIG(status) );
        else if( WIFCONTINUED(status) )
            printf("Child %ld continued\n", (long) child_pid );*/
        try {
            devicePtr->sendChildNotification(devicePtr->_identifier, "Unknown", "Unknown");
        } catch ( ... ) {
        }
    }
    
    if( child_pid == -1 && errno != ECHILD )
    {
        // Error
        perror("waitpid");
    }
    
    errno = saved_errno;
}

std::vector<int> GPP_i::getPids()
{
    boost::mutex::scoped_lock lock(pidLock);
    std::vector<int> keys;
    for (std::map<int, component_description_struct>::iterator it=pids.begin();it!=pids.end();it++) {
        keys.push_back(it->first);
    }
    return keys;
}

void GPP_i::addPid(int pid, std::string appName, std::string identifier)
{
    boost::mutex::scoped_lock lock(pidLock);
    if (pids.find(pid) == pids.end()) {
        component_description_struct tmp;
        tmp.appName = appName;
        tmp.identifier = identifier;
        pids[pid] = tmp;
    }
}

component_description_struct GPP_i::getComponentDescription(int pid)
{
    boost::mutex::scoped_lock lock(pidLock);
    std::map<int, component_description_struct>::iterator it=pids.find(pid);
    if (it == pids.end())
        throw std::invalid_argument("pid not found");
    return it->second;
}

void GPP_i::removePid(int pid)
{
    boost::mutex::scoped_lock lock(pidLock);
    std::map<int, component_description_struct>::iterator it=pids.find(pid);
    if (it == pids.end())
        return;
    pids.erase(it);
}

GPP_i::GPP_i(char *devMgr_ior, char *id, char *lbl, char *sftwrPrfl) :
    GPP_base(devMgr_ior, id, lbl, sftwrPrfl)
{
}

GPP_i::GPP_i(char *devMgr_ior, char *id, char *lbl, char *sftwrPrfl, char *compDev) :
    GPP_base(devMgr_ior, id, lbl, sftwrPrfl, compDev)
{
}

GPP_i::GPP_i(char *devMgr_ior, char *id, char *lbl, char *sftwrPrfl, CF::Properties capacities) :
    GPP_base(devMgr_ior, id, lbl, sftwrPrfl, capacities)
{
}

GPP_i::GPP_i(char *devMgr_ior, char *id, char *lbl, char *sftwrPrfl, CF::Properties capacities, char *compDev) :
    GPP_base(devMgr_ior, id, lbl, sftwrPrfl, capacities, compDev)
{
}

GPP_i::~GPP_i()
{
}

void GPP_i::addReservation(component_description_struct component)
{
    boost::mutex::scoped_lock lock(pidLock);
    this->reservations.push_back(component);
}

void GPP_i::removeReservation(component_description_struct component)
{
    boost::mutex::scoped_lock lock(pidLock);
    std::vector<component_description_struct>::iterator it = std::find(this->reservations.begin(), this->reservations.end(), component);
    if (it != this->reservations.end()) {
        this->reservations.erase(it);
    }
    it = std::find(this->tabled_reservations.begin(), this->tabled_reservations.end(), component);
    if (it != this->tabled_reservations.end()) {
        this->tabled_reservations.erase(it);
    }
}

void GPP_i::shiftReservation(component_description_struct component)
{
    boost::mutex::scoped_lock lock(pidLock);
    std::vector<component_description_struct>::iterator it = std::find(this->reservations.begin(), this->reservations.end(), component);
    if (it != this->reservations.end()) {
        this->tabled_reservations.push_back(*it);
        this->reservations.erase(it);
    }
}

void GPP_i::shiftReservationBack(component_description_struct component)
{
    boost::mutex::scoped_lock lock(pidLock);
    std::vector<component_description_struct>::iterator it = std::find(this->tabled_reservations.begin(), this->tabled_reservations.end(), component);
    if (it != this->tabled_reservations.end()) {
        this->reservations.push_back(*it);
        this->tabled_reservations.erase(it);
    }
}

void GPP_i::initialize() throw (CF::LifeCycle::InitializeError, CORBA::SystemException)
{
    // Install signal handler to properly handle SIGCHLD signals
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigchld_handler;
    if( sigaction(SIGCHLD, &sa, NULL) == -1 )
    {
        perror("sigaction");
        LOG_ERROR(GPP_i, __FUNCTION__ << ": Error registering SIGCHLD handler");
    }
    this->setAllocationImpl("nic_allocation", this, &GPP_i::allocateCapacity_nic_allocation, &GPP_i::deallocateCapacity_nic_allocation);

    setAllocationImpl("DCE:72c1c4a9-2bcf-49c5-bafd-ae2c1d567056", this, &GPP_i::allocate_loadCapacity, &GPP_i::deallocate_loadCapacity);
    this->processor_cores = boost::thread::hardware_concurrency();
    
    addPropertyChangeListener("reserved_capacity_per_component", this, &GPP_i::reservedChanged);
    this->idle_capacity_modifier = this->reserved_capacity_per_component/((float)this->processor_cores);
    this->modified_thresholds = this->thresholds;
    
    char hostname[HOST_NAME_MAX];
    int retval = gethostname(hostname, HOST_NAME_MAX);
    if (!retval) {
        this->hostName = hostname;
    }
    
    // figure out the location of the binary
    std::ostringstream szTmp;
    szTmp << "/proc/"<<getpid()<<"/exe";
    char pBuff[PATH_MAX];
    unsigned int len = PATH_MAX-1;
    int bytes = readlink(szTmp.str().c_str(), pBuff, len);
    if (bytes >= 0)
        pBuff[bytes] = '\0';
    std::string binary_file(pBuff);
    binary_location = binary_file.substr(0, binary_file.find_last_of('/')+1);
    
    threshold_monitors.clear();
    initializeCpuMonitor();
    initializeNetworkMonitor();
    // update states
    std::for_each( states.begin(), states.end(), boost::bind( &State::update_state, _1 ) );
    GPP_base::start();
    GPP_base::initialize();
}

bool GPP_i::allocateCapacity_nic_allocation(const nic_allocation_struct &alloc)
{
    LOG_TRACE(GPP_i, __FUNCTION__ << ": Allocating nic_allocation (identifier=" << alloc.identifier << ")");
    try
    {
        LOG_DEBUG(GPP_i, __FUNCTION__ << ": ALLOCATION: { identifier: \"" << alloc.identifier << "\", data_rate: " << alloc.data_rate << ", data_size: " << alloc.data_size << ", multicast_support: \"" << alloc.multicast_support << "\", ip_addressable: \"" << alloc.ip_addressable << "\", interface: \"" << alloc.interface << "\" }");
        bool success = nic_facade->allocate_capacity(alloc);
        
        if( success )
        {
            nic_allocation_status_struct_struct status;
            for( size_t i=0; i<nic_allocation_status.size(); ++i )
            {
                if( nic_allocation_status[i].identifier == alloc.identifier )
                {
                    status = nic_allocation_status[i];
                    LOG_DEBUG(GPP_i, __FUNCTION__ << ": SUCCESS: { identifier: \"" << status.identifier << "\", data_rate: " << status.data_rate << ", data_size: " << status.data_size << ", multicast_support: \"" << status.multicast_support << "\", ip_addressable: \"" << status.ip_addressable << "\", interface: \"" << status.interface << "\" }");
                    break;
                }
            }
        }
        
        return success;
    }
    //catch( const NicAllocator::InvalidAllocation& e )
    catch( ... )
    {
        CF::Properties errprops;
        errprops.length(1);
        errprops[0].id = "nic_allocation";
        errprops[0].value <<= alloc;
        throw CF::Device::InvalidCapacity("Invalid allocation", errprops);
    }
}
void GPP_i::deallocateCapacity_nic_allocation(const nic_allocation_struct &alloc)
{
    LOG_TRACE(GPP_i, __FUNCTION__ << ": Deallocating nic_allocation (identifier=" << alloc.identifier << ")");
    try
    {
        LOG_DEBUG(GPP_i, __FUNCTION__ << ": { identifier: \"" << alloc.identifier << "\", data_rate: " << alloc.data_rate << ", data_size: " << alloc.data_size << ", multicast_support: \"" << alloc.multicast_support << "\", ip_addressable: \"" << alloc.ip_addressable << "\", interface: \"" << alloc.interface << "\" }");
        nic_facade->deallocate_capacity(alloc);
    }
    catch( ... )
    {
        CF::Properties errprops;
        errprops.length(1);
        errprops[0].id = "nic_allocation";
        errprops[0].value <<= alloc;
        throw CF::Device::InvalidCapacity("Invalid allocation", errprops);
    }
}

void GPP_i::reservedChanged(const float *oldValue, const float *newValue)
{
    this->idle_capacity_modifier = this->reserved_capacity_per_component/((float)this->processor_cores);
}

void
GPP_i::initializeNetworkMonitor()
{
    nic_facade.reset( new NicFacade(advanced.maximum_throughput_percentage,
                                    nic_interfaces,
                                    available_nic_interfaces,
                                    networkMonitor,
                                    nic_metrics,
                                    nic_allocation_status) );
    states.push_back( nic_facade );
    statistics.push_back( nic_facade );
    reports.push_back( nic_facade );

    std::vector<std::string> nic_devices( nic_facade->get_devices() );
    for( size_t i=0; i<nic_devices.size(); ++i )
    {
        LOG_INFO(GPP_i, __FUNCTION__ << ": Adding interface (" << nic_devices[i] << ")" );
        addThresholdMonitor(
            new NicThroughputThresholdMonitor(_identifier,
                                              nic_devices[i],
                                              MakeCref<CORBA::Long, float>(modified_thresholds.nic_usage),
                                              boost::bind(&NicFacade::get_throughput_by_device, nic_facade, nic_devices[i]) ) );
    }
}

void
GPP_i::initializeCpuMonitor()
{
    cpu_state.reset(new CpuState());
    states.push_back( cpu_state );

    boost::shared_ptr<CpuUsageAccumulator> cpu_usage_accumulator(
        new CpuUsageAccumulator(cpu_state) );
    statistics.push_back( cpu_usage_accumulator );
    
    boost::shared_ptr<SystemMonitorReporting> system_monitor_reporting( 
        new SystemMonitorReporting( cpu_usage_accumulator, system_monitor ) );
    reports.push_back( system_monitor_reporting );
    
    addThresholdMonitor( new CpuThresholdMonitor(_identifier, &modified_thresholds.cpu_idle, *cpu_usage_accumulator ) );
}

void
GPP_i::addThresholdMonitor( ThresholdMonitor* threshold_monitor )
{
	boost::shared_ptr<ThresholdMonitor> t( threshold_monitor );
	t->attach_listener( boost::bind(&GPP_i::send_threshold_event, this, _1) );
	threshold_monitors.push_back( t );
}

void GPP_i::send_threshold_event(const threshold_event_struct& message)
{
	LOG_INFO(GPP_i, __FUNCTION__ << ": " << message.message );
	MessageEvent_out->sendMessage(message);
}

void GPP_i::sendChildNotification(std::string dev_id, std::string comp_id, std::string app_id)
{
    StandardEvent::AbnormalComponentTerminationEventType event;
    event.deviceId = CORBA::string_dup(dev_id.c_str());
    event.componentId = CORBA::string_dup(comp_id.c_str());
    event.applicationId = CORBA::string_dup(app_id.c_str());
    CORBA::Any outboundMessage;
    outboundMessage <<= event;
    try {
        this->proxy_consumer->push(outboundMessage);
    } catch ( ... ) {
        LOG_WARN(GPP_i, "Unable to send a child termination notification on the IDM channel");
    }
}

bool GPP_i::allocate_loadCapacity(const double &value) {
    std::ifstream file("/proc/loadavg");
    double current_load;
    std::vector<std::string> items;
    file>>current_load;
    if ((current_load+value) > (this->processor_cores * this->loadCapacityPerCore * (this->loadThreshold / 100.0)))
        return false;
    
    return true;
}

void GPP_i::deallocate_loadCapacity(const double &value) {
    return;
}

CF::ExecutableDevice::ProcessID_Type GPP_i::execute (const char* name, const CF::Properties& options, const CF::Properties& parameters)
    throw (CORBA::SystemException, CF::Device::InvalidState, CF::ExecutableDevice::InvalidFunction, 
           CF::ExecutableDevice::InvalidParameters, CF::ExecutableDevice::InvalidOptions, 
           CF::InvalidFileName, CF::ExecutableDevice::ExecuteFail)
{
    std::vector<std::string> prepend_args;
    std::string naming_context_ior;
    const redhawk::PropertyMap& tmp_params = redhawk::PropertyMap::cast(parameters);
    naming_context_ior = tmp_params["NAMING_CONTEXT_IOR"].toString();
    std::string app_id;
    std::string component_id = tmp_params["COMPONENT_IDENTIFIER"].toString();
    std::string name_binding = tmp_params["NAME_BINDING"].toString();
    CF::Application_var _app = CF::Application::_nil();
    CORBA::Object_var obj = ossie::corba::Orb()->string_to_object(naming_context_ior.c_str());
    if (CORBA::is_nil(obj)) {
        LOG_WARN(GPP_i, "Invalid application registrar IOR");
    } else {
        CF::ApplicationRegistrar_var _appRegistrar = CF::ApplicationRegistrar::_nil();
        _appRegistrar = CF::ApplicationRegistrar::_narrow(obj);
        if (CORBA::is_nil(_appRegistrar)) {
            LOG_WARN(GPP_i, "Invalid application registrar IOR");
        } else {
            _app = _appRegistrar->app();
            if (not CORBA::is_nil(_app)) {
                app_id = ossie::corba::returnString(_app->name());
            }
        }
    }
    if (this->useScreen) {
        std::string ld_lib_path(getenv("LD_LIBRARY_PATH"));
        setenv("GPP_LD_LIBRARY_PATH",ld_lib_path.c_str(),1);
        
        DIR *dir;
        struct dirent *ent;
        std::string search_bin("screen");
    
        std::string path(getenv( "PATH" ));
        bool foundScreen = false;
        while (not foundScreen) {
            size_t sub = path.find(":");
            if (path.size() == 0)
                break;
            std::string substr = path.substr(0, sub);
            if ((dir = opendir (substr.c_str())) != NULL) {
                while ((ent = readdir (dir)) != NULL) {
                    std::string filename(ent->d_name);
                    if (filename == search_bin) {
                        prepend_args.push_back(substr+"/"+filename);
                        foundScreen = true;
                        break;
                    }
                }
                closedir (dir);
            }
            if (sub != std::string::npos)
                path = path.substr(sub+1, std::string::npos);
            else
                path.clear();
        }
        prepend_args.push_back("-D");
        prepend_args.push_back("-m");
        prepend_args.push_back("-c");
        prepend_args.push_back(binary_location+"gpp.screenrc");
        if ((not component_id.empty()) and (not name_binding.empty())) {
            if (component_id.find("DCE:") != std::string::npos) {
                component_id = component_id.substr(4, std::string::npos);
            }
            size_t waveform_boundary = component_id.find(":");
            std::string component_inst_id, waveform_name;
            component_inst_id = component_id.substr(0, waveform_boundary);
            waveform_name = component_id.substr(waveform_boundary+1, std::string::npos);
            prepend_args.push_back("-S");
            prepend_args.push_back(waveform_name+"."+name_binding);
            prepend_args.push_back("-t");
            prepend_args.push_back(waveform_name+"."+name_binding);
        }
    }
    CF::ExecutableDevice::ProcessID_Type ret_pid;
    try {
        ret_pid = ExecutableDevice_impl::do_execute(name, options, parameters, prepend_args);
        this->addPid(ret_pid, app_id, component_id);
        this->addReservation(this->pids[ret_pid]);
    } catch ( ... ) {
        throw;
    }
    return ret_pid;
}

void GPP_i::terminate (CF::ExecutableDevice::ProcessID_Type processId) throw (CORBA::SystemException, CF::ExecutableDevice::InvalidProcess, CF::Device::InvalidState)
{
    ExecutableDevice_impl::terminate(processId);
    this->removeReservation(this->pids[processId]);
    this->removePid(processId);
}

void GPP_i::updateUsageState()
{
	if (system_monitor.idle_cpu_percent < modified_thresholds.cpu_idle)
		setUsageState(CF::Device::BUSY);
	else if (system_monitor.physical_memory_free < (unsigned long)modified_thresholds.mem_free)
		setUsageState(CF::Device::BUSY);
	else if (this->getPids().size() == 0)
		setUsageState(CF::Device::IDLE);
	else
		setUsageState(CF::Device::ACTIVE);
}

void GPP_i::updateThresholdMonitors()
{
	for( size_t i=0; i<threshold_monitors.size(); ++i )
	{
		threshold_monitors[i]->update();
		LOG_TRACE(GPP_i, __FUNCTION__ << ": resource_id=" << threshold_monitors[i]->get_resource_id() << " threshold=" << threshold_monitors[i]->get_threshold() << " measured=" << threshold_monitors[i]->get_measured())
	}
}

void GPP_i::establishModifiedThresholds()
{
    this->modified_thresholds = this->thresholds;
    CF::DomainManager::ApplicationSequence_var apps = new CF::DomainManager::ApplicationSequence();
    std::map<std::string,bool> checked_apps;
    if (this->reservations.size() > 0) {
        std::vector<component_description_struct> original_res = reservations;
        for (std::vector<component_description_struct>::iterator it=original_res.begin();it!=original_res.end();it++) {
            if (applications.find((*it).appName) == applications.end()) {
                if (apps->length() == 0) {
                    apps = this->getDomainManager()->getRef()->applications();
                }
                for (unsigned int i=0; i<apps->length(); i++) {
                    std::string appname = ossie::corba::returnString(apps[i]->name());
                    if (appname == (*it).appName) {
                        applications[appname] = CF::Application::_duplicate(apps[i]);
                        break;
                    }
                }
            }
            bool started = false;
            if (checked_apps.find((*it).appName) == checked_apps.end()) {
                try {
                    started = applications[(*it).appName]->started();
                    checked_apps[(*it).appName] = started;
                } catch ( ... ) {
                    // The application went away between getting the reference and now
                    // This will clean up when terminate is called on the device
                }
            } else {
                started = checked_apps[(*it).appName];
            }
            if (started) {
                this->shiftReservation((*it));
            }
        }
    }
    if (this->tabled_reservations.size() > 0) {
        std::vector<component_description_struct> original_res = tabled_reservations;
        for (std::vector<component_description_struct>::iterator it=original_res.begin();it!=original_res.end();it++) {
            if (applications.find((*it).appName) == applications.end()) {
                if (apps->length() == 0) {
                    apps = this->getDomainManager()->getRef()->applications();
                }
                for (unsigned int i=0; i<apps->length(); i++) {
                    std::string appname = ossie::corba::returnString(apps[i]->name());
                    if (appname == (*it).appName) {
                        applications[appname] = CF::Application::_duplicate(apps[i]);
                        break;
                    }
                }
            }
            bool started = false;
            if (checked_apps.find((*it).appName) == checked_apps.end()) {
                try {
                    started = applications[(*it).appName]->started();
                    checked_apps[(*it).appName] = started;
                } catch ( ... ) {
                    // The application went away between getting the reference and now
                    // This will clean up when terminate is called on the device
                }
            } else {
                started = checked_apps[(*it).appName];
            }
            if (!started) {
                this->shiftReservationBack((*it));
            }
        }
    }
    this->modified_thresholds.cpu_idle = this->thresholds.cpu_idle + (this->idle_capacity_modifier * this->reservations.size());
}

void GPP_i::calculateSystemMemoryLoading() {
	memCapacity = system_monitor.physical_memory_free;
}

int GPP_i::serviceFunction()
{
	usleep(1000000);
    
    establishModifiedThresholds();
    
    try
	{
        // update states
        std::for_each( states.begin(), states.end(), boost::bind( &State::update_state, _1 ) );
        // compute statistics
        std::for_each( statistics.begin(), statistics.end(), boost::bind( &Statistics::compute_statistics, _1 ) );
        // compile reports
        std::for_each( reports.begin(), reports.end(), boost::bind( &Reporting::report, _1 ) );
        
        calculateSystemMemoryLoading();
	}
    catch( const boost::thread_resource_error& e )
    {
        std::stringstream errstr;
        errstr << "Error acquiring lock (errno=" << e.native_error() << " msg=\"" << e.what() << "\")";
        LOG_ERROR(GPP_i, __FUNCTION__ << ": " << errstr.str() );
        return NOOP;
    }

	for( size_t i=0; i<threshold_monitors.size(); ++i )
	{
		threshold_monitors[i]->update();
		LOG_TRACE(GPP_i, __FUNCTION__ << ": resource_id=" << threshold_monitors[i]->get_resource_id() << " threshold=" << threshold_monitors[i]->get_threshold() << " measured=" << threshold_monitors[i]->get_measured())
	}

	updateThresholdMonitors();

	updateUsageState();
    
    return NOOP;
}

