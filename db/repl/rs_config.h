// rs_config.h
// repl set configuration
//

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

#include "../../util/hostandport.h"
#include "health.h"

namespace mongo { 

    const string rsConfigNs = "local.system.replset";

    class ReplSetConfig {
        enum { EMPTYCONFIG = -2 };
    public:
        /* if something is misconfigured, throws an exception. 
        if couldn't be queried or is just blank, ok() will be false.
        */
        ReplSetConfig(const HostAndPort& h);

        ReplSetConfig(BSONObj cfg);

        bool ok() const { return _ok; }

        struct MemberCfg {
            MemberCfg() : _id(-1), votes(1), priority(1.0), arbiterOnly(false) { }
            int _id;              /* ordinal */
            unsigned votes;       /* how many votes this node gets. default 1. */
            HostAndPort h;
            double priority;      /* 0 means can never be primary */
            bool arbiterOnly;
            void check() const;   /* check validity, assert if not. */
            BSONObj asBson() const;
            bool hot() const { 
                return !arbiterOnly;
            }
        };
        vector<MemberCfg> members;
        string _id;
        int version;
        HealthOptions ho;
        string md5;
        BSONObj getLastErrorDefaults;

        list<HostAndPort> otherMemberHostnames() const; // except self

        /** @return true if could connect, and there is no cfg object there at all */
        bool empty() const { return version == EMPTYCONFIG; }

        string toString() const { return asBson().toString(); }

        /** validate the settings. does not call check() on each member, you have to do that separately. */
        void check() const;

        static void receivedNewConfig(BSONObj);
        void saveConfigLocally(); // to local db
        string saveConfigEverywhere(); // returns textual info on what happened

        BSONObj asBson() const;

    private:
        bool _ok;
        void from(BSONObj);
        void clear();
    };

}
