/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <cooling_policy.h>

static struct cooling_rule rule = {.enabled=true,.restore_performance=true,
    .warm=90,.hot=95,.warm_seconds=300,.hot_seconds=30,.balanced_seconds=600,.saver_seconds=600};
static enum cooling_reason why;
static enum cooling_profile step(struct cooling_state *s, double t, enum cooling_profile p, int sec) {
    return cooling_decide(s,&rule,t,p,(gint64)sec*G_USEC_PER_SEC,(gint64)4*G_USEC_PER_SEC,&why);
}
static void applied(struct cooling_state *s, enum cooling_profile p, int sec) {
    cooling_applied(s,&rule,p,why,(gint64)sec*G_USEC_PER_SEC);
}
int main(void) {
    struct cooling_state s={0};
    for(int i=1;i<=2000;i++) assert(step(&s,89.9,COOL_PERFORMANCE,i)==COOL_PERFORMANCE);
    assert(!s.hold); /* no five-minute budget below 90 */
    for(int i=2001;i<2301;i++) assert(step(&s,90,COOL_PERFORMANCE,i)==COOL_NONE);
    assert(step(&s,95,COOL_PERFORMANCE,2301)==COOL_BALANCED);applied(&s,COOL_BALANCED,2301);
    assert(s.hold_until==(gint64)2901*G_USEC_PER_SEC);
    for(int i=2302;i<2901;i++) {
        assert(step(&s,50,COOL_BALANCED,i)==COOL_BALANCED);
        applied(&s,COOL_BALANCED,i);assert(s.hold_until==(gint64)2901*G_USEC_PER_SEC);
    }
    assert(step(&s,89,COOL_BALANCED,2901)==COOL_PERFORMANCE);applied(&s,COOL_PERFORMANCE,2901);
    assert(!s.hold);
    s=(struct cooling_state){0};
    for(int i=1;i<31;i++) assert(step(&s,95.1,COOL_PERFORMANCE,i)==COOL_NONE);
    assert(step(&s,95.1,COOL_PERFORMANCE,31)==COOL_SAVER);applied(&s,COOL_SAVER,31);
    assert(s.hold_until==(gint64)631*G_USEC_PER_SEC);
    for(int i=32;i<=630;i++) assert(step(&s,92,COOL_SAVER,i)==COOL_SAVER);
    assert(step(&s,92,COOL_SAVER,631)==COOL_BALANCED);applied(&s,COOL_BALANCED,631);
    for(int i=632;i<650;i++) assert(step(&s,96,COOL_BALANCED,i)==COOL_BALANCED);
    assert(step(&s,95,COOL_BALANCED,650)==COOL_BALANCED); /* exactly 95 is warm */
    for(int i=651;i<681;i++) assert(step(&s,96,COOL_BALANCED,i)==COOL_BALANCED);
    assert(step(&s,96,COOL_BALANCED,681)==COOL_SAVER);applied(&s,COOL_SAVER,681);
    assert(s.hold_until==(gint64)1281*G_USEC_PER_SEC);
    for(int i=682;i<=1281;i++) assert(step(&s,97,COOL_SAVER,i)==COOL_SAVER);
    applied(&s,COOL_SAVER,1281);assert(s.hold_until==(gint64)1281*G_USEC_PER_SEC); /* still hot, no promotion or extra ten-minute extension */
    assert(step(&s,NAN,COOL_SAVER,1282)==COOL_NONE);assert(!s.hot_since&&!s.warm_since);
    assert(step(&s,UNKNOWN_DBL_VALUE,COOL_SAVER,1283)==COOL_NONE);
    assert(step(&s,50,COOL_NONE,1284)==COOL_NONE);
    s=(struct cooling_state){0};
    for(int i=1;i<=299;i++) assert(step(&s,92,COOL_PERFORMANCE,i)==COOL_NONE);
    assert(step(&s,92,COOL_PERFORMANCE,310)==COOL_NONE); /* missing samples restart dwell */
    for(int i=311;i<610;i++) assert(step(&s,92,COOL_PERFORMANCE,i)==COOL_NONE);
    assert(step(&s,92,COOL_PERFORMANCE,610)==COOL_BALANCED);applied(&s,COOL_BALANCED,610);
    assert(step(&s,UNKNOWN_DBL_VALUE,COOL_BALANCED,1211)==COOL_NONE); /* no unsafe recovery */
    assert(step(&s,92,COOL_BALANCED,1212)==COOL_BALANCED);applied(&s,COOL_BALANCED,1212);
    assert(s.hold_until==(gint64)1210*G_USEC_PER_SEC);
    assert(step(&s,89,COOL_BALANCED,1213)==COOL_PERFORMANCE); /* after minimum hold, recovery is immediate */
    rule.restore_performance=false;
    assert(step(&s,50,COOL_BALANCED,1813)==COOL_NONE);
    rule.enabled=false;assert(step(&s,100,COOL_BALANCED,1814)==COOL_NONE);
    rule.enabled=true;rule.hot_seconds=0;s=(struct cooling_state){0};
    assert(step(&s,96,COOL_BALANCED,1)==COOL_SAVER);
    puts("PASS: exact 90/95 boundaries, 5-minute dwell, 30-second emergency, 10-minute minimum, escalation priority, persistent heat, cool recovery, stale/invalid data, disabled/manual recovery");
}
