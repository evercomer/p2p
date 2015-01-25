create database if not exists account character set utf8;

use account;
--
-- Table structure for table `account`
--

DROP TABLE IF EXISTS `account`;
CREATE TABLE `account` (
  `uid` int(11) NOT NULL,
  `user` varchar(20) NOT NULL,
  `pswd` varchar(20) NOT NULL,
  `flag` int(11) default '0',
  `name` varchar(32) default NULL,
  `email` varchar(40) default NULL,
  `regdate` date default NULL,
  PRIMARY KEY  (`uid`),
  UNIQUE KEY `idx_user` (`user`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8;


--
-- Table structure for table `camgrp`
--

DROP TABLE IF EXISTS `camgrp`;
CREATE TABLE `camgrp` (
  `uid` int(11) default NULL,
  `gid` int(11) default NULL,
  `name` char(48) default NULL,
  UNIQUE KEY `idx_uid_gid` (`uid`,`gid`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8;


--
-- Table structure for table `dname`
--

DROP TABLE IF EXISTS `dname`;
CREATE TABLE `dname` (
  `dname` varchar(40) NOT NULL,
  `uid` int(11) default NULL,
  `regdate` date default NULL,
  PRIMARY KEY  (`dname`),
  KEY `idx_uid` (`uid`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8;


--
-- Table structure for table `ipcam`
--

DROP TABLE IF EXISTS `ipcam`;
CREATE TABLE `ipcam` (
  `uid` int(11) default NULL,
  `gid` int(11) default NULL,
  `sn` char(16) NOT NULL,
  `name` char(48) default NULL,
  `regdate` date default NULL,
  `expire` date default NULL,
  PRIMARY KEY  (`sn`),
  UNIQUE KEY `idx_uid_sn` (`uid`,`sn`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8;

