/*
 * EquivalenceClass.cpp
 *
 *  Created on: Mar 15, 2012
 *      Author: khurshid
 *
 * This file is protected by the VeriFlow Research License Agreement
 * available at http://www.cs.illinois.edu/~khurshi1/projects/veriflow/veriflow-research-license-agreement.txt.
 * A copy of this agreement is also included in this package.
 *
 * Copyright (c) 2012-2013 by
 * The Board of Trustees of the University of Illinois.
 * All rights reserved.
 */

#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include "EquivalenceClass.h"
#include "VeriFlow.h"

using namespace std;

EquivalenceClass::EquivalenceClass()
{
	this->clear();
}

EquivalenceClass::EquivalenceClass(const uint64_t* lb, const uint64_t* ub)
{
	for(int i = 0; i < ALL_FIELD_INDEX_END_MARKER; i++)
	{
		if(lb[i] > ub[i])
		{
			fprintf(stderr, "[EquivalenceClass::EquivalenceClass] Invalid boundary values for field %d (lb > ub). Terminating process.\n", i);
			fprintf(stderr, "\tLower bound: %lu\n", lb[i]);
			fprintf(stderr, "\tUpper bound: %lu\n", ub[i]);

			exit(1);
		}

		this->lowerBound[i] = lb[i];
		this->upperBound[i] = ub[i];
	}
}

uint64_t EquivalenceClass::getRange(int fieldIndex) const
{
	return (this->upperBound[fieldIndex] - this->lowerBound[fieldIndex]);
}

bool EquivalenceClass::equals(const EquivalenceClass& other) const
{
	for(int i = 0; i < ALL_FIELD_INDEX_END_MARKER; i++)
	{
		if((this->lowerBound[i] != other.lowerBound[i]) || (this->upperBound[i] != other.upperBound[i]))
		{
			return false;
		}
	}

	return true;
}

bool EquivalenceClass::operator==(const EquivalenceClass& other) const
{
	return this->equals(other);
}

bool EquivalenceClass::subsumes(const EquivalenceClass &other) const
{
	for(int i = 0; i < ALL_FIELD_INDEX_END_MARKER; i++)
	{
		if((this->lowerBound[i] <= other.lowerBound[i] && this->upperBound[i] >= other.upperBound[i]))
		{
			//Okay
		} else {
			return false;
		}
	}

	return true;
	
}

int EquivalenceClass::operator()() const
{
	int retVal = 0;
	for(int i = 0; i < ALL_FIELD_INDEX_END_MARKER; i++)
	{
		retVal += (this->lowerBound[i] + this->upperBound[i]);
	}

	return retVal;
}

bool EquivalenceClass::intersects(const EquivalenceClass& other) const {
	for (int i=0; i< ALL_FIELD_INDEX_END_MARKER; i++){
		if (this->upperBound[i] < other.lowerBound[i] || this->lowerBound[i] > other.upperBound[i]){
			return false;
		}
	}
	return true;
}

vector<EquivalenceClass> EquivalenceClass::subtract(const EquivalenceClass& other) const {
	vector<EquivalenceClass> result;
	
	// 如果不相交，直接返回当前等价类
	if (!this->intersects(other)) {
		result.push_back(*this);
		return result;
	}
	
	// 创建一个临时等价类用于存储中间结果
	EquivalenceClass temp = *this;
	
	// 对每个字段进行处理
	for (int i = 0; i < ALL_FIELD_INDEX_END_MARKER; i++) {
		// 如果当前字段有重叠
		if (temp.lowerBound[i] <= other.upperBound[i] && temp.upperBound[i] >= other.lowerBound[i]) {
			// 处理下界部分
			if (temp.lowerBound[i] < other.lowerBound[i]) {
				EquivalenceClass left = temp;
				left.upperBound[i] = other.lowerBound[i] - 1;
				result.push_back(left);
			}
			
			// 处理上界部分
			if (temp.upperBound[i] > other.upperBound[i]) {
				EquivalenceClass right = temp;
				right.lowerBound[i] = other.upperBound[i] + 1;
				result.push_back(right);
			}
			
			// 更新临时等价类的边界为重叠部分
			temp.lowerBound[i] = max(temp.lowerBound[i], other.lowerBound[i]);
			temp.upperBound[i] = min(temp.upperBound[i], other.upperBound[i]);
		}
	}
	
	return result;
}

void EquivalenceClass::clear()
{
	for(int i = 0; i < ALL_FIELD_INDEX_END_MARKER; i++)
	{
		this->lowerBound[i] = 0;
		this->upperBound[i] = 0;
	}
}
/*
string EquivalenceClass::toString() const
{
	char buffer[1024];
	sprintf(buffer, "[EquivalenceClass] dl_src (%lu-%s, %lu-%s), dl_dst (%lu-%s, %lu-%s)",
			this->lowerBound[DL_SRC], ::getMacValueAsString(this->lowerBound[DL_SRC]).c_str(),
			this->upperBound[DL_SRC], ::getMacValueAsString(this->upperBound[DL_SRC]).c_str(),
			this->lowerBound[DL_DST], ::getMacValueAsString(this->lowerBound[DL_DST]).c_str(),
			this->upperBound[DL_DST], ::getMacValueAsString(this->upperBound[DL_DST]).c_str());

	string retVal = buffer;
	retVal += ", ";

	sprintf(buffer, "nw_src (%lu-%s, %lu-%s), nw_dst (%lu-%s, %lu-%s)",
			this->lowerBound[NW_SRC], ::getIpValueAsString(this->lowerBound[NW_SRC]).c_str(),
			this->upperBound[NW_SRC], ::getIpValueAsString(this->upperBound[NW_SRC]).c_str(),
			this->lowerBound[NW_DST], ::getIpValueAsString(this->lowerBound[NW_DST]).c_str(),
			this->upperBound[NW_DST], ::getIpValueAsString(this->upperBound[NW_DST]).c_str());

	retVal += buffer;
	retVal += ", ";

	for(int i = 0; i < ALL_FIELD_INDEX_END_MARKER; i++)
	{
		sprintf(buffer, "Field %d (%lu, %lu)", i, this->lowerBound[i], this->upperBound[i]);
		retVal += buffer;

		if(i < (ALL_FIELD_INDEX_END_MARKER - 1))
		{
			retVal += ", ";
		}
	}

	return retVal;
}
*/

string EquivalenceClass::toString() const
{
	char buffer[1024];

	sprintf(buffer, "[EquivalenceClass] dl_src (%s, %s), dl_dst (%s, %s)",
			::getMacValueAsString(this->lowerBound[DL_SRC]).c_str(),
			::getMacValueAsString(this->upperBound[DL_SRC]).c_str(),
			::getMacValueAsString(this->lowerBound[DL_DST]).c_str(),
			::getMacValueAsString(this->upperBound[DL_DST]).c_str());
	string retVal = buffer;
	retVal += ", ";

	sprintf(buffer, "[EquivalenceClass] nw_src (%s, %s), nw_dst (%s, %s)",
			::getIpValueAsString(this->lowerBound[NW_SRC]).c_str(),
			::getIpValueAsString(this->upperBound[NW_SRC]).c_str(),
			::getIpValueAsString(this->lowerBound[NW_DST]).c_str(),
			::getIpValueAsString(this->upperBound[NW_DST]).c_str());
	retVal += buffer;
	retVal += ", ";

	sprintf(buffer, "nw_proto(%lu, %lu)", this->lowerBound[NW_PROTO], this->upperBound[NW_PROTO]);
	retVal += buffer;
	retVal += ", ";

	sprintf(buffer, "tp_src(%lu, %lu)", this->lowerBound[TP_SRC], this->upperBound[TP_SRC]);
	retVal += buffer;
	retVal += ", ";

	sprintf(buffer, "tp_dst(%lu, %lu)", this->lowerBound[TP_DST], this->upperBound[TP_DST]);
	retVal += buffer;
	retVal += ", ";

	return retVal;
}

uint64_t EquivalenceClass::getMaxValue(FieldIndex index)
{
	switch(index)
	{
	case IN_PORT:
		return 0xFFFF;
		break;

	case DL_SRC:
		return 0xFFFFFFFFFFFFLLU;
		break;

	case DL_DST:
		return 0xFFFFFFFFFFFFLLU;
		break;

	case DL_TYPE:
		return 0xFFFF;
		break;

	case DL_VLAN:
		return 0xFFF;
		break;

	case DL_VLAN_PCP:
		return 0x7;
		break;

	case MPLS_LABEL:
		return 0xFFFFF;
		break;

	case MPLS_TC:
		return 0x7;
		break;

	case NW_SRC:
		return 0xFFFFFFFF;
		break;

	case NW_DST:
		return 0xFFFFFFFF;
		break;

	case NW_PROTO:
		return 0xFF;
		break;

	case NW_TOS:
		return 0x3F;
		break;

	case TP_SRC:
		return 0xFFFF;
		break;

	case TP_DST:
		return 0xFFFF;
		break;

	case METADATA:
		return 0xFFFFFFFFFFFFFFFFLLU;
		break;

	default:
		fprintf(stderr, "[EquivalenceClass::getMaxValue] Wrong field index (%d) specified. Terminating process.\n", index);
		exit(1);
		return 0x0; // Something went wrong; wrong field index was specified. Unreachable code due to the presence of exit(1).
		break;
	}
}
