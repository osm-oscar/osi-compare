#include "static-htm-index.h"
#include <sserialize/strings/unicode_case_functions.h>

#include "HtmSpatialGrid.h"
#include "H3SpatialGrid.h"

namespace hic::Static {
	
namespace ssinfo::SpatialGridInfo {


sserialize::UByteArrayAdapter::SizeType
MetaData::getSizeInBytes() const {
	return 3+m_d->trixelId2HtmIndexId().getSizeInBytes()+m_d->htmIndexId2TrixelId().getSizeInBytes()+m_d->trixelItemIndexIds().getSizeInBytes();;
}

sserialize::UByteArrayAdapter::SizeType
MetaData::offset(DataMembers member) const {
	switch(member) {
		case DataMembers::type:
			return 1;
		case DataMembers::levels:
			return 2;
		case DataMembers::trixelId2HtmIndexId:
			return 3;
		case DataMembers::htmIndexId2TrixelId:
			return 3+m_d->trixelId2HtmIndexId().getSizeInBytes();
		case DataMembers::trixelItemIndexIds:
			return 3+m_d->trixelId2HtmIndexId().getSizeInBytes()+m_d->htmIndexId2TrixelId().getSizeInBytes();
		default:
			throw sserialize::InvalidEnumValueException("MetaData");
			break;
	};
	return 0;
}


Data::Data(const sserialize::UByteArrayAdapter & d) :
m_type(decltype(m_type)(d.getUint8(1))),
m_levels(d.getUint8(2)),
m_trixelId2HtmIndexId(d+3),
m_htmIndexId2TrixelId(d+(3+m_trixelId2HtmIndexId.getSizeInBytes())),
m_trixelItemIndexIds(d+(3+m_trixelId2HtmIndexId.getSizeInBytes()+m_htmIndexId2TrixelId.getSizeInBytes()))
{}

} //end namespace ssinfo::HtmInfo
	
SpatialGridInfo::SpatialGridInfo(const sserialize::UByteArrayAdapter & d) :
m_d(d)
{}

SpatialGridInfo::~SpatialGridInfo() {}

sserialize::UByteArrayAdapter::SizeType
SpatialGridInfo::getSizeInBytes() const {
	return MetaData(&m_d).getSizeInBytes();
}

int SpatialGridInfo::levels() const {
	return m_d.levels();
}

SpatialGridInfo::SizeType
SpatialGridInfo::cPixelCount() const {
	return m_d.trixelId2HtmIndexId().size();
}

SpatialGridInfo::ItemIndexId
SpatialGridInfo::itemIndexId(CPixelId trixelId) const {
	return m_d.trixelItemIndexIds().at(trixelId);
}

SpatialGridInfo::CPixelId
SpatialGridInfo::cPixelId(SGPixelId htmIndex) const {
	return m_d.htmIndexId2TrixelId().at(htmIndex);
}

SpatialGridInfo::SGPixelId
SpatialGridInfo::sgIndex(CPixelId cPixelId) const {
	return m_d.trixelId2HtmIndexId().at64(cPixelId);
}

OscarSearchSgIndex::OscarSearchSgIndex(const sserialize::UByteArrayAdapter & d, const sserialize::Static::ItemIndexStore & idxStore) :
m_sq(d.at(1)),
m_sgInfo( d+2 ),
m_trie( Trie::PrivPtrType(new FlatTrieType(d+(2+sgInfo().getSizeInBytes()))) ),
m_idxStore(idxStore)
{
	SSERIALIZE_VERSION_MISSMATCH_CHECK(MetaData::version, d.at(0), "hic::Static::OscarSearchSgIndex");
	switch(sgInfo().type()) {
		case SpatialGridInfo::MetaData::SG_HTM:
			m_sg = hic::HtmSpatialGrid::make(sgInfo().levels());
			break;
		case SpatialGridInfo::MetaData::SG_H3:
			m_sg = hic::H3SpatialGrid::make(sgInfo().levels());
			break;
		default:
			throw sserialize::TypeMissMatchException("SpatialGridType is invalid: " + std::to_string(sgInfo().type()));
			break;
	};
}


sserialize::RCPtrWrapper<OscarSearchSgIndex>
OscarSearchSgIndex::make(const sserialize::UByteArrayAdapter & d, const sserialize::Static::ItemIndexStore & idxStore) {
    return sserialize::RCPtrWrapper<OscarSearchSgIndex>( new OscarSearchSgIndex(d, idxStore) );
}

OscarSearchSgIndex::~OscarSearchSgIndex() {}

sserialize::UByteArrayAdapter::SizeType
OscarSearchSgIndex::getSizeInBytes() const {
    return 0;
}

sserialize::Static::ItemIndexStore const &
OscarSearchSgIndex::idxStore() const {
    return m_idxStore;
}

int
OscarSearchSgIndex::flags() const {
    return m_flags;
}

std::ostream &
OscarSearchSgIndex::printStats(std::ostream & out) const {
	out << "OscarSearchSgIndex::BEGIN_STATS" << std::endl;
	m_trie.printStats(out);
	out << "OscarSearchSgIndex::END_STATS" << std::endl;
	return out;
}

sserialize::StringCompleter::SupportedQuerries
OscarSearchSgIndex::getSupportedQueries() const {
    return sserialize::StringCompleter::SupportedQuerries(m_sq);
}

OscarSearchSgIndex::Payload::Type
OscarSearchSgIndex::typeFromCompletion(const std::string& qs, const sserialize::StringCompleter::QuerryType qt) const {
	std::string qstr;
	if (m_sq & sserialize::StringCompleter::SQ_CASE_INSENSITIVE) {
		qstr = sserialize::unicode_to_lower(qs);
	}
	else {
		qstr = qs;
	}
	Payload p( m_trie.at(qstr, (qt & sserialize::StringCompleter::QT_SUBSTRING || qt & sserialize::StringCompleter::QT_PREFIX)) );
	Payload::Type t;
	if (p.types() & qt) {
		t = p.type(qt);
	}
	else if (qt & sserialize::StringCompleter::QT_SUBSTRING) {
		if (p.types() & sserialize::StringCompleter::QT_PREFIX) { //exact suffix matches are either available or not
			t = p.type(sserialize::StringCompleter::QT_PREFIX);
		}
		else if (p.types() & sserialize::StringCompleter::QT_SUFFIX) {
			t = p.type(sserialize::StringCompleter::QT_SUFFIX);
		}
		else if (p.types() & sserialize::StringCompleter::QT_EXACT) {
			t = p.type(sserialize::StringCompleter::QT_EXACT);
		}
		else {
			throw sserialize::OutOfBoundsException("OscarSearchSgIndex::typeFromCompletion");
		}
	}
	else if (p.types() & sserialize::StringCompleter::QT_EXACT) { //qt is either prefix, suffix, exact
		t = p.type(sserialize::StringCompleter::QT_EXACT);
	}
	else {
		throw sserialize::OutOfBoundsException("OscarSearchSgIndex::typeFromCompletion");
	}
	return t;
}

//BEGIN SgOpTree

SgOpTree::SgOpTree(sserialize::RCPtrWrapper<hic::Static::OscarSearchSgIndex> const & d) :
m_d(d)
{}

sserialize::CellQueryResult
SgOpTree::calc() {
    return Calc(m_d).calc(root());
}

SgOpTree::Calc::CQRType
SgOpTree::Calc::Calc::calc(const Node * node) {
    if (!node) {
        return CQRType();
    }
	switch (node->baseType) {
	case Node::LEAF:
		switch (node->subType) {
		case Node::STRING:
		case Node::STRING_REGION:
		case Node::STRING_ITEM:
		{
			if (!node->value.size()) {
				return CQRType();
			}
			const std::string & str = node->value;
			std::string qstr(str);
			sserialize::StringCompleter::QuerryType qt = sserialize::StringCompleter::QT_NONE;
			qt = sserialize::StringCompleter::normalize(qstr);
			if (node->subType == Node::STRING_ITEM) {
				throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: item string query");
			}
			else if (node->subType == Node::STRING_REGION) {
				throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: region string query");
			}
			else {
				return m_d->complete<CQRType>(qstr, qt);
			}
		}
		case Node::REGION:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: region query");
		case Node::REGION_EXCLUSIVE_CELLS:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: region exclusive cells");
		case Node::CELL:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: cell");
		case Node::CELLS:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: cells");
		case Node::RECT:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: rectangle");
		case Node::POLYGON:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: polygon");
		case Node::PATH:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: path");
		case Node::POINT:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: point");
		case Node::ITEM:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: item");
		default:
			break;
		};
		break;
	case Node::UNARY_OP:
		switch(node->subType) {
		case Node::FM_CONVERSION_OP:
			return calc(node->children.at(0)).allToFull();
		case Node::CELL_DILATION_OP:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: cell dilation");
		case Node::REGION_DILATION_OP:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: region dilation");
		case Node::COMPASS_OP:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: compass");
		case Node::IN_OP:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: in query");
		case Node::NEAR_OP:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: near query");
		case Node::RELEVANT_ELEMENT_OP:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: relevant item query");
		case Node::QUERY_EXCLUSIVE_CELLS:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: query exclusive cells");
		default:
			break;
		};
		break;
	case Node::BINARY_OP:
		switch(node->subType) {
		case Node::SET_OP:
			switch (node->value.at(0)) {
			case '+':
				return calc(node->children.front()) + calc(node->children.back());
			case '/':
			case ' ':
				return calc(node->children.front()) / calc(node->children.back());
			case '-':
				return calc(node->children.front()) - calc(node->children.back());
			case '^':
				return calc(node->children.front()) ^ calc(node->children.back());
			default:
				return CQRType();
			};
			break;
		case Node::BETWEEN_OP:
			throw sserialize::UnsupportedFeatureException("OscarSearchWithSg: between query");
		default:
			break;
		};
		break;
	default:
		break;
	};
	return CQRType();
}

//END SgOpTree

//BEGIN Static::detail::OscarSearchSgIndexCellInfo
namespace detail {


OscarSearchSgIndexCellInfo::OscarSearchSgIndexCellInfo(const sserialize::RCPtrWrapper<IndexType> & d) :
m_d(d)
{}
OscarSearchSgIndexCellInfo::~OscarSearchSgIndexCellInfo()
{}


OscarSearchSgIndexCellInfo::RCType
OscarSearchSgIndexCellInfo::makeRc(const sserialize::RCPtrWrapper<IndexType> & d) {
	return sserialize::RCPtrWrapper<sserialize::interface::CQRCellInfoIface>( new OscarSearchSgIndexCellInfo(d) );
}

OscarSearchSgIndexCellInfo::SizeType
OscarSearchSgIndexCellInfo::cellSize() const {
	return m_d->sgInfo().cPixelCount();
}
sserialize::spatial::GeoRect
OscarSearchSgIndexCellInfo::cellBoundary(CellId cellId) const {
	return m_d->sg().bbox(m_d->sgInfo().sgIndex(cellId));
}

OscarSearchSgIndexCellInfo::SizeType
OscarSearchSgIndexCellInfo::cellItemsCount(CellId cellId) const {
	return m_d->idxStore().idxSize(cellItemsPtr(cellId));
}

OscarSearchSgIndexCellInfo::IndexId
OscarSearchSgIndexCellInfo::cellItemsPtr(CellId cellId) const {
	return m_d->sgInfo().itemIndexId(cellId);
}

}//end namespace detail
//END Static::detail::OscarSearchSgIndexCellInfo

//BEGIN OscarSearchSgCompleter

void
OscarSearchSgCompleter::energize(std::string const & files) {
	auto indexData = sserialize::UByteArrayAdapter::openRo(files + "/index", false);
	auto searchData = sserialize::UByteArrayAdapter::openRo(files + "/search", false);
	auto idxStore = sserialize::Static::ItemIndexStore(indexData);
	m_d = hic::Static::OscarSearchSgIndex::make(searchData, idxStore);
}

sserialize::CellQueryResult
OscarSearchSgCompleter::complete(std::string const & str) {
	SgOpTree opTree(m_d);
	opTree.parse(str);
	return opTree.calc();
}

//END OscarSearchSgCompleter
	
}//end namespace hic::Static
