#ifndef TASKWORKER_HPP
#define TASKWORKER_HPP

#include <string>
#include <thread>
#include "DatabaseAPI.hpp"
#include "GlobusAPI.hpp"
#include "ITaskMgr.hpp"
#include "ITaskWorker.hpp"

namespace SDMS {
namespace Core {

class TaskWorker : public ITaskWorker
{
public:

    TaskWorker( ITaskMgr & a_mgr, uint32_t a_id );
    ~TaskWorker();

private:

    void        workerThread();
    bool        handleDataGet();
    bool        handleDataPut();
    bool        handleDataDelete();
    bool        handleRecordChangeAlloc();
    bool        handleRecordChangeOwner();
    bool        handleRecordDelete();
    bool        handleProjectDelete();
    bool        handleAllocCreate();
    bool        handleAllocDelete();
    void        getUserAccessToken( const std::string & a_uid );
    bool        checkEncryption( Encryption a_encrypt, const GlobusAPI::EndpointInfo & a_ep_info );
    void        monitorTransfer();
    bool        refreshDataSize( const std::string & a_repo_id, const std::string & a_data_id, const std::string & a_data_path, const std::string & a_src_path, const libjson::Value & a_ext );
    bool        repoSendRecv( const std::string & a_repo_id, MsgBuf::Message & a_msg, MsgBuf::Message *& a_reply );
    void        finalizeTask( bool a_succeeded, const std::string & a_msg );

    ITaskMgr &                  m_mgr;
    std::thread *               m_thread;
    ITaskMgr::Task *            m_task;
    DatabaseAPI                 m_db;
    GlobusAPI                   m_glob;
    std::string                 m_glob_task_id;
    std::string                 m_access_token;
};

}}

#endif
