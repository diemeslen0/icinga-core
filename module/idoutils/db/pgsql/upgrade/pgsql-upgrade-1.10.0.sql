-- -----------------------------------------
-- upgrade path for Icinga IDOUtils 1.9.0
--
-- -----------------------------------------
-- Copyright (c) 2012-2013 Icinga Development Team (http://www.icinga.org)
--
-- Please check http://docs.icinga.org for upgrading information!
-- -----------------------------------------

-- -----------------------------------------
-- #4363 deprecate enable_sla
-- -----------------------------------------

-- DROP TABLE icinga_slahistory;

-- -----------------------------------------
-- #4420 "integer" out-of-range
-- -----------------------------------------

CREATE OR REPLACE FUNCTION from_unixtime(bigint) RETURNS timestamp with time zone AS '
	SELECT to_timestamp($1) AS result
' LANGUAGE sql;

ALTER TABLE icinga_downtimehistory ALTER COLUMN duration TYPE BIGINT;
ALTER TABLE icinga_scheduleddowntime ALTER COLUMN duration TYPE BIGINT;

-- -----------------------------------------
-- #4482 deprecate timedevent* tables
-- -----------------------------------------

-- drop index too, if seperate tbs
ALTER TABLE icinga_timedevents DROP INDEX timedevents_i_id_idx;
ALTER TABLE icinga_timedevents DROP INDEX timedevents_time_id_idx;
ALTER TABLE icinga_timedevents DROP INDEX timed_e_event_type_idx;
ALTER TABLE icinga_timedevents DROP INDEX timed_e_object_id_idx;
ALTER TABLE icinga_timedevents DROP INDEX timed_e_rec_ev_idx;

ALTER TABLE icinga_timedeventqueue DROP INDEX timedeventq_i_id_idx;
ALTER TABLE icinga_timedeventqueue DROP INDEX timedeventq_time_id_idx;
ALTER TABLE icinga_timedeventqueue DROP INDEX timedeventqueue_i_id_idx;
ALTER TABLE icinga_timedeventqueue DROP INDEX timed_e_q_event_type_idx;
ALTER TABLE icinga_timedeventqueue DROP INDEX timed_e_q_sched_time_idx;
ALTER TABLE icinga_timedeventqueue DROP INDEX timed_e_q_object_id_idx;
ALTER TABLE icinga_timedeventqueue DROP INDEX timed_e_q_rec_ev_id_idx;

DROP TABLE icinga_timedevents;
DROP TABLE icinga_timedeventqueue;

-- -----------------------------------------
-- update dbversion
-- -----------------------------------------

SELECT updatedbversion('1.10.0');

