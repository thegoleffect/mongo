// /db/repl/rs.h

/**
*    Copyright (C) 2008 10gen Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "../../util/concurrency/list.h"
#include "../../util/concurrency/value.h"
#include "../../util/concurrency/msg.h"
#include "../../util/hostandport.h"
#include "../commands.h"
#include "rs_optime.h"
#include "rsmember.h"
#include "rs_config.h"

namespace mongo {

    struct Target;
    class ReplSetImpl;
    extern bool replSet; // true if using repl sets
    extern class ReplSet *theReplSet; // null until initialized
    extern Tee *rsLog;

    class Member : public List1<Member>::Base {
    public:
        Member(HostAndPort h, unsigned ord, const ReplSetConfig::MemberCfg *c);

        string fullName() const { return h().toString(); }
        const ReplSetConfig::MemberCfg& config() const { return *_config; }
        const HeartbeatInfo& hbinfo() const { return _hbinfo; }
        string lhb() { return _hbinfo.lastHeartbeatMsg; }
        MemberState state() const { return _hbinfo.hbstate; }
        const HostAndPort& h() const { return _h; }
        unsigned id() const { return _hbinfo.id(); }
        bool hot() const { return _config->hot(); }

        void summarizeAsHtml(stringstream& s) const;
        friend class ReplSetImpl;
    private:
        const ReplSetConfig::MemberCfg *_config; /* todo: when this changes??? */
        HostAndPort _h;
        HeartbeatInfo _hbinfo;
    };

    class Manager : public task::Server {
        bool got(const any&);
        ReplSetImpl *rs;
        int _primary;
        const Member* findOtherPrimary();
        void noteARemoteIsPrimary(const Member *);
    public:
        Manager(ReplSetImpl *rs);
        void msgReceivedNewConfig(BSONObj) { assert(false); }
        void msgCheckNewState();
    };

    class Consensus {
        ReplSetImpl &rs;
        struct LastYea { 
            LastYea() : when(0), who(0xffffffff) { }
            time_t when;
            unsigned who;
        };
        Atomic<LastYea> ly;
        unsigned yea(unsigned memberId); // throws VoteException
        void _electSelf();
        bool weAreFreshest(bool& allUp);
    public:
        Consensus(ReplSetImpl *t) : rs(*t) { }
        int totalVotes() const;
        bool aMajoritySeemsToBeUp() const;
        void electSelf();
        void electCmdReceived(BSONObj, BSONObjBuilder*);
    };

    /** most operations on a ReplSet object should be done while locked. */
    class RSBase : boost::noncopyable { 
    private:
        mutex m;
        int _locked;
    protected:
        RSBase() : m("RSBase"), _locked(0) { }
        class lock : scoped_lock { 
            RSBase& _b;
        public:
            lock(RSBase* b) : scoped_lock(b->m), _b(*b) { 
                DEV assert(b->_locked == 0);
                b->_locked++; 
            }
            ~lock() { 
                DEV assert(_b._locked == 1);
                _b._locked--; 
            }
        };
        bool locked() const { return _locked != 0; }
    };

    /* information about the entire repl set, such as the various servers in the set, and their state */
    /* note: We currently do not free mem when the set goes away - it is assumed the replset is a 
             singleton and long lived.
    */
    class ReplSetImpl : RSBase {
    public:
        /** info on our state if the replset isn't yet "up".  for example, if we are pre-initiation. */
        enum StartupStatus { 
            PRESTART=0, LOADINGCONFIG=1, BADCONFIG=2, EMPTYCONFIG=3, 
            EMPTYUNREACHABLE=4, STARTED=5, SOON=6 
        };
        static StartupStatus startupStatus;
        static string startupStatusMsg;
        static string stateAsStr(MemberState state);
        static string stateAsHtml(MemberState state);

        /* todo thread */
        void msgUpdateHBInfo(HeartbeatInfo);
        bool isPrimary() const { return _myState == PRIMARY; }
        bool isSecondary() const { return _myState == SECONDARY; }

    private:
        Consensus elect;
        bool ok() const { return _myState != FATAL; }

        void relinquish();
        void assumePrimary();

    protected:
        void _fillIsMaster(BSONObjBuilder&);
        const ReplSetConfig& config() { return *_cfg; }
        string name() const { return _name; } /* @return replica set's logical name */
        MemberState state() const { return _myState; }        
        void _fatal();
        void _getOplogDiagsAsHtml(unsigned server_id, stringstream& ss) const;
        void _summarizeAsHtml(stringstream&) const;        
        void _summarizeStatus(BSONObjBuilder&) const; // for replSetGetStatus command

        /* cfgString format is 
           replsetname/host1,host2:port,...
           where :port is optional.
           throws exception if a problem initializing. */
        ReplSetImpl(string cfgString);

        /* call after constructing to start - returns fairly quickly after launching its threads */
        void _go();

    private:

        MemberState _myState;
        string _name;
        const vector<HostAndPort> *_seeds;
        ReplSetConfig *_cfg;

        /** load our configuration from admin.replset.  try seed machines too. 
            throws exception if a problem.
        */
        void _loadConfigFinish(vector<ReplSetConfig>& v);
        void loadConfig();
        void initFromConfig(ReplSetConfig& c);//, bool save);

        list<HostAndPort> memberHostnames() const;
        const Member* currentPrimary() const { return _currentPrimary; }
        const ReplSetConfig::MemberCfg& myConfig() const { return _self->config(); }

    private:
        const Member *_currentPrimary;
        Member *_self;        
        List1<Member> _members; /* all members of the set EXCEPT self. */

    public:
        shared_ptr<Manager> mgr;

    private:
        Member* head() const { return _members.head(); }
        Member* findById(unsigned id) const;
        void getTargets(list<Target>&);
        void startThreads();
        friend class FeedbackThread;
        friend class CmdReplSetElect;
        friend class Member;
        friend class Manager;
        friend class Consensus;
    };

    class ReplSet : public ReplSetImpl { 
    public:
        ReplSet(string cfgString) : ReplSetImpl(cfgString) { }
        /* call after constructing to start - returns fairly quickly after launching its threads */
        void go() { _go(); }
        void fatal() { _fatal(); }
        bool isMaster(const char *client);
        MemberState state() const { return ReplSetImpl::state(); }
        string name() const { return ReplSetImpl::name(); }
        const ReplSetConfig& config() { return ReplSetImpl::config(); }
        void getOplogDiagsAsHtml(unsigned server_id, stringstream& ss) const { _getOplogDiagsAsHtml(server_id,ss); }
        void summarizeAsHtml(stringstream& ss) const { _summarizeAsHtml(ss); }
        void summarizeStatus(BSONObjBuilder& b) const  { _summarizeStatus(b); }
        void fillIsMaster(BSONObjBuilder& b) { _fillIsMaster(b); }
    };

    /** base class for repl set commands.  checks basic things such as in rs mode before the command 
        does its real work
        */
    class ReplSetCommand : public Command { 
    protected:
        ReplSetCommand(const char * s, bool show=false) : Command(s) { }
        virtual bool slaveOk() const { return true; }
        virtual bool adminOnly() const { return true; }
        virtual bool logTheOp() { return false; }
        virtual LockType locktype() const { return NONE; }
        virtual void help( stringstream &help ) const { help << "internal"; }
        bool check(string& errmsg, BSONObjBuilder& result) {
            if( !replSet ) { 
                errmsg = "not running with --replSet";
                return false;
            }
            if( theReplSet == 0 ) {
                result.append("startupStatus", ReplSet::startupStatus);
                errmsg = ReplSet::startupStatusMsg.empty() ? "replset unknown error 2" : ReplSet::startupStatusMsg;
                return false;
            }
            return true;
        }
    };
    
    /** inlines ----------------- */

    inline Member::Member(HostAndPort h, unsigned ord, const ReplSetConfig::MemberCfg *c) : 
        _config(c), _h(h), _hbinfo(ord) { }

    inline bool ReplSet::isMaster(const char *client) {         
        /* todo replset */
        return false;
    }

}
