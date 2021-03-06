/**
 * @file
 * RuleTable is a thread-safe store used for storing
 * and retrieving message bus routing rules.
 */

/******************************************************************************
 *
 *
 *    Copyright (c) Open Connectivity Foundation (OCF), AllJoyn Open Source
 *    Project (AJOSP) Contributors and others.
 *
 *    SPDX-License-Identifier: Apache-2.0
 *
 *    All rights reserved. This program and the accompanying materials are
 *    made available under the terms of the Apache License, Version 2.0
 *    which accompanies this distribution, and is available at
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Copyright (c) Open Connectivity Foundation and Contributors to AllSeen
 *    Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for
 *    any purpose with or without fee is hereby granted, provided that the
 *    above copyright notice and this permission notice appear in all
 *    copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *    WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *    WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 *    AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 *    DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 *    PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 *    TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 *    PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/
#ifndef _ALLJOYN_RULETABLE_H
#define _ALLJOYN_RULETABLE_H

#include <qcc/platform.h>
#include <qcc/Mutex.h>
#include <qcc/LockLevel.h>

#include "BusEndpoint.h"
#include "Rule.h"


namespace ajn {

/** Rule iterator */
typedef std::multimap<BusEndpoint, Rule>::iterator RuleIterator;
typedef std::multimap<BusEndpoint, Rule>::const_iterator RuleConstIterator;

/**
 * RuleTable is a thread-safe store used for storing
 * and retrieving message bus routing rules.
 */
class RuleTable {
  public:

    /**
     * Constructor.
     */
    RuleTable() : lock(qcc::LOCK_LEVEL_RULETABLE_LOCK) { }

    /**
     * Add a rule for an endpoint.
     *
     * @param endpoint   The endpoint that this rule applies to.
     * @param rule       Rule for endpoint
     * @return ER_OK if successful;
     */
    QStatus AddRule(BusEndpoint& endpoint, const Rule& rule);

    /**
     * Remove a rule for an endpoint.
     *
     * @param endpoint   The endpoint that rule applies to.
     * @param rule       Rule to remove.
     * @return ER_OK if successful;
     */
    QStatus RemoveRule(BusEndpoint& endpoint, Rule& rule);

    /**
     * Remove all rules for a given endpoint.
     *
     * @param endpoint    Endpoint whose rules will be removed.
     * @return ER_OK if successful;
     */
    QStatus RemoveAllRules(BusEndpoint& endpoint);

    /**
     * Obtain exclusive access to rule table.
     * This method only needs to be called before using methods that return or use
     * AllJoynRuleIterators. Atomic rule table operations will obtain the lock internally.
     *
     * @return ER_OK if successful.
     */
    QStatus Lock() { return lock.Lock(MUTEX_CONTEXT); }

    /**
     * Release exclusive access to rule table.
     *
     * @return ER_OK if successful.
     */
    QStatus Unlock() { return lock.Unlock(MUTEX_CONTEXT); }

    /**
     * Return an iterator to the start of the rules.
     * Caller should obtain lock before calling this method.
     * @return Iterator to first rule.
     */
    RuleIterator Begin() { return rules.begin(); }

    /**
     * Return an iterator to the end of the rules.
     * @return Iterator to end of rules.
     */
    RuleIterator End() { return rules.end(); }

    /**
     * Find all rules for a given endpoint.
     * Caller should obtain lock before calling this method.
     *
     * @param endpoint  Endpoint whose rules are needed.
     * @return  Iterator to first rule for given endpoint.
     */
    RuleIterator FindRulesForEndpoint(BusEndpoint& endpoint) {
        return rules.find(endpoint);
    }

    /**
     * Advance iterator to next endpoint.
     *
     * @param endpoint   Endpoint before advance.
     * @return   Iterator to next endpoint in ruleTable or end.
     *
     */
    RuleIterator AdvanceToNextEndpoint(BusEndpoint endpoint) {
        std::multimap<BusEndpoint, Rule>::iterator ret = rules.upper_bound(endpoint);
        return ret;
    }

    /**
     * Check if message matches a rule for the given endpoint.
     *
     * @param   msg         Message that may be delivered.
     * @param   endpoint    Endpoint message may be delivered to.
     *
     * @return  true if endpoint has a match rule that matches the message, false otherwise.
     */
    bool OkToSend(const Message& msg, BusEndpoint& endpoint) const;

  private:
    mutable qcc::Mutex lock;                   /**< Lock protecting rule table */
    std::multimap<BusEndpoint, Rule> rules;    /**< Rule table */
};

}

#endif
