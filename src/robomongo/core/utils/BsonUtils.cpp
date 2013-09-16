#include "robomongo/core/utils/BsonUtils.h"

#include <mongo/client/dbclient.h>
#include <mongo/bson/bsonobjiterator.h>
#include "robomongo/core/utils/QtUtils.h"
#include "robomongo/core/HexUtils.h"
#include "mongo/util/base64.h"
#include "robomongo/shell/db/ptimeutil.h"

using namespace mongo;
namespace Robomongo
{
    namespace BsonUtils
    {
        namespace detail
        {
            template<>
            mongo::BSONObj getField<mongo::BSONObj>(const mongo::BSONElement &elem) 
            {
                return elem.embeddedObject();
            }

            template<>
            bool getField<bool>(const mongo::BSONElement &elem)
            {
                return elem.Bool();
            }

            template<>
            std::string getField<std::string>(const mongo::BSONElement &elem)
            {
                return elem.String();
            }

            template<>
            std::vector<BSONElement> getField<std::vector<BSONElement> >(const mongo::BSONElement &elem)
            {
                return elem.Array();
            }

            template<>
            int getField<int>(const mongo::BSONElement &elem)
            {
                return elem.Int();
            }

            template<>
            double getField<double>(const mongo::BSONElement &elem)
            {
                return elem.numberDouble();
            }

            template<>
            long long getField<long long>(const mongo::BSONElement &elem)
            {
                return elem.safeNumberLong();
            }
        }

        std::string jsonString(BSONObj &obj, JsonStringFormat format, int pretty, UUIDEncoding uuidEncoding, SupportedTimes timeFormat)
        {
            if ( obj.isEmpty() ) return "{}";

            StringBuilder s;
            s << "{";
            BSONObjIterator i(obj);
            BSONElement e = i.next();
            if ( !e.eoo() ){
                while ( 1 ) {
                    if ( pretty ) {
                        s << '\n';
                        for( int x = 0; x < pretty; x++ ){
                            s << "    ";
                        }
                    }
                    else {
                        s << " ";
                    }
                    s << jsonString(e, format, true, pretty?pretty+1:0, uuidEncoding, timeFormat);
                    e = i.next();

                    if (e.eoo()) {
                        s << '\n';
                        for( int x = 0; x < pretty - 1; x++ ){
                            s << "    ";
                        }
                        s << "}";
                        break;
                    }

                    s << ",";
                }
            }
            return s.str();
        }

        std::string jsonString(BSONElement &elem, JsonStringFormat format, bool includeFieldNames, int pretty, UUIDEncoding uuidEncoding, SupportedTimes timeFormat)
        {
            BSONType t = elem.type();
            if ( t == Undefined )
                return "undefined";

            stringstream s;
            if ( includeFieldNames )
                s << '"' << escape( elem.fieldName() ) << "\" : ";

            switch ( t ) {
            case mongo::String:
            case Symbol:
                s << '"' << escape( string(elem.valuestr(), elem.valuestrsize()-1) ) << '"';
                break;
            case NumberLong:
                s << "NumberLong(" << elem._numberLong() << ")";
                break;
            case NumberInt:
            case NumberDouble:
                {
                    int sign=0;
                    if ( elem.number() >= -numeric_limits< double >::max() &&
                            elem.number() <= numeric_limits< double >::max() ) {
                        s.precision( 16 );
                        s << elem.number();
                    }
                    else if ( mongo::isNaN(elem.number()) ) {
                        s << "NaN";
                    }
                    else if ( mongo::isInf(elem.number(), &sign) ) {
                        s << ( sign == 1 ? "Infinity" : "-Infinity");
                    }
                    else {
                        StringBuilder ss;
                        ss << "Number " << elem.number() << " cannot be represented in JSON";
                        string message = ss.str();
                        //massert( 10311 ,  message.c_str(), false );
                    }
                    break;
                }
            case mongo::Bool:
                s << ( elem.boolean() ? "true" : "false" );
                break;
            case jstNULL:
                s << "null";
                break;
            case Object: {
                BSONObj obj = elem.embeddedObject();
                s << jsonString(obj, format, pretty, uuidEncoding,timeFormat);
                }
                break;
            case mongo::Array: {
                if ( elem.embeddedObject().isEmpty() ) {
                    s << "[]";
                    break;
                }
                s << "[ ";
                BSONObjIterator i( elem.embeddedObject() );
                BSONElement e = i.next();
                if ( !e.eoo() ) {
                    int count = 0;
                    while ( 1 ) {
                        if( pretty ) {
                            s << '\n';
                            for( int x = 0; x < pretty; x++ )
                                s << "    ";
                        }

                        if (strtol(e.fieldName(), 0, 10) > count) {
                            s << "undefined";
                        }
                        else {
                            s << jsonString(e, format, false, pretty?pretty+1:0, uuidEncoding, timeFormat);
                            e = i.next();
                        }
                        count++;
                        if ( e.eoo() ) {
                            s << '\n';
                            for( int x = 0; x < pretty - 1; x++ )
                                s << "    ";
                            s << "]";
                            break;
                        }
                        s << ", ";
                    }
                }
                //s << " ]";
                break;
            }
            case DBRef: {
                mongo::OID *x = (mongo::OID *) (elem.valuestr() + elem.valuestrsize());
                if ( format == TenGen )
                    s << "DBRef(";
                else
                    s << "{ \"$ref\" : ";
                s << '"' << elem.valuestr() << "\", ";
                if ( format != TenGen )
                    s << "\"$id\" : ";
                s << '"' << *x << "\"";
                if ( format == TenGen )
                    s << ')';
                else
                    s << '}';
                break;
            }
            case jstOID:
                if ( format == TenGen ) {
                    s << "ObjectId(";
                }
                else {
                    s << "{ \"$oid\" : ";
                }
                s << '"' << elem.__oid() << '"';
                if ( format == TenGen ) {
                    s << ")";
                }
                else {
                    s << " }";
                }
                break;
            case BinData: {
                int len = *(int *)( elem.value() );
                BinDataType type = BinDataType( *(char *)( (int *)( elem.value() ) + 1 ) );

                if (type == mongo::bdtUUID || type == mongo::newUUID) {
                    s << HexUtils::formatUuid(elem, uuidEncoding);
                    break;
                }

                s << "{ \"$binary\" : \"";
                char *start = ( char * )( elem.value() ) + sizeof( int ) + 1;
                base64::encode( s , start , len );
                s << "\", \"$type\" : \"" << hex;
                s.width( 2 );
                s.fill( '0' );
                s << type << dec;
                s << "\" }";
                break;
            }
            case mongo::Date:
                {
                    Date_t d = elem.date();
                    long long ms = static_cast<long long>(d.millis);
                    bool isSupportedDate = miutil::minDate < ms && ms < miutil::maxDate;

                    if ( format == Strict )
                        s << "{ \"$date\" : ";
                    else{
                        if(isSupportedDate){
                            s << "ISODate(";
                        }
                        else{
                            s << "Date(";
                        }
                    }

                    if ( pretty && isSupportedDate) {
                        boost::posix_time::ptime epoch(boost::gregorian::date(1970,1,1));
                        boost::posix_time::time_duration diff = boost::posix_time::millisec(ms);
                        boost::posix_time::ptime time = epoch + diff;
                        std::string timestr = miutil::isotimeString(time, true, timeFormat == LocalTime);
                        s << '"' << timestr << '"';
                    }
                    else
                        s << ms;

                    if ( format == Strict )
                        s << " }";
                    else
                        s << ")";
                    break;
                }
            case RegEx:
                if ( format == Strict ) {
                    s << "{ \"$regex\" : \"" << escape( elem.regex() );
                    s << "\", \"$options\" : \"" << elem.regexFlags() << "\" }";
                }
                else {
                    s << "/" << escape( elem.regex() , true ) << "/";
                    // FIXME Worry about alpha order?
                    for ( const char *f = elem.regexFlags(); *f; ++f ) {
                        switch ( *f ) {
                        case 'g':
                        case 'i':
                        case 'm':
                            s << *f;
                        default:
                            break;
                        }
                    }
                }
                break;

            case CodeWScope: {
                BSONObj scope = elem.codeWScopeObject();
                if ( ! scope.isEmpty() ) {
                    s << "{ \"$code\" : " << elem._asCode() << " , "
                      << " \"$scope\" : " << scope.jsonString() << " }";
                    break;
                }
            }

            case Code:
                s << elem._asCode();
                break;

            case Timestamp:
                if ( format == TenGen ) {
                    s << "Timestamp(" << ( elem.timestampTime() / 1000 ) << ", " << elem.timestampInc() << ")";
                }
                else {
                    s << "{ \"$timestamp\" : { \"t\" : " << ( elem.timestampTime() / 1000 ) << ", \"i\" : " << elem.timestampInc() << " } }";
                }
                break;

            case MinKey:
                s << "{ \"$minKey\" : 1 }";
                break;

            case MaxKey:
                s << "{ \"$maxKey\" : 1 }";
                break;

            default:
                StringBuilder ss;
                ss << "Cannot create a properly formatted JSON string with "
                   << "element: " << elem.toString() << " of type: " << elem.type();
            }
            return s.str();
        }
    
        bool isArray(const mongo::BSONElement &elem)
        {
            return elem.isABSONObj() && elem.type() == mongo::Array;
        }

        bool isDocument(const mongo::BSONElement &elem)
        {
            return elem.isABSONObj();
        }

        bool isSimpleType(const mongo::BSONType type)
        {
            switch( type ) {
            case NumberLong:
            case NumberDouble:
            case NumberInt:
            case mongo::String:
            case mongo::Bool:
            case mongo::Date:
            case jstOID:
                return true;
            default:
                return false;
            }
        }

        bool isUuidType(const mongo::BSONType type, mongo::BinDataType binDataType)
        {
            if (type != mongo::BinData)
                return false;

            return (binDataType == mongo::newUUID || binDataType == mongo::bdtUUID);
        }

        bool isSimpleType(const mongo::BSONElement &elem) 
        {
            return isSimpleType(elem.type()); 
        }

        bool isUuidType(const mongo::BSONElement &elem) 
        {
            if (elem.type() != mongo::BinData)
                return false;

            mongo::BinDataType binType = elem.binDataType();
            return (binType == mongo::newUUID || binType == mongo::bdtUUID);
        }

        const char* BSONTypeToString(mongo::BSONType type, mongo::BinDataType binDataType, UUIDEncoding uuidEncoding)
        {
            switch (type)
            {
                /** double precision floating point value */
            case NumberDouble:
                {
                    return "Double";
                }
                /** character string, stored in utf8 */
            case String:
                {
                    return "String";
                }

                /** an embedded object */
            case Object:
                {
                    return "Object";
                }
                /** an embedded array */
            case Array:
                {
                    return "Array";
                }
            case BinData:
                {
                    if (binDataType == mongo::newUUID) {
                        return "UUID";
                    } else if (binDataType == mongo::bdtUUID) {
                        const char* type;
                        switch(uuidEncoding) {
                        case DefaultEncoding: type = "Legacy UUID"; break;
                        case JavaLegacy:      type = "Java UUID (Legacy)"; break;
                        case CSharpLegacy:    type = ".NET UUID (Legacy)"; break;
                        case PythonLegacy:    type = "Python UUID (Legacy)"; break;
                        default:              type = "Legacy UUID"; break;
                        }

                        return type;
                    } else {
                        return "Binary";
                    }
                }

                /** Undefined type */
            case Undefined:
                {
                    return "Undefined";
                }

                /** ObjectId */
            case jstOID:
                {
                    return "ObjectId";
                }

                /** boolean type */
            case Bool:
                {
                    return "Boolean";
                }

                /** date type */
            case Date:
                {
                    return "Date";
                }

                /** null type */
            case jstNULL:
                {
                    return "Null";
                }
                break;

                /** regular expression, a pattern with options */
            case RegEx:
                {
                    return "Regular Expression";
                }

                /** deprecated / will be redesigned */
            case DBRef:
                {
                    return "DBRef";
                }

                /** deprecated / use CodeWScope */
            case Code:
                {
                    return "Code";
                }
                break;

                /** a programming language (e.g., Python) symbol */
            case Symbol:
                {
                    return "Symbol";
                }

                /** javascript code that can execute on the database server, with SavedContext */
            case CodeWScope:
                {
                    return "CodeWScope";
                }

                /** 32 bit signed integer */
            case NumberInt:
                {
                    return "Int32";
                }

                /** Updated to a Date with value next OpTime on insert */
            case Timestamp:
                {
                    return "Timestamp";
                }

                /** 64 bit integer */
            case NumberLong:
                {
                    return "Int64";
                }
                break;

            default:
                {
                    return "Type is not supported";
                }
            }
        }

        std::string bsonArrayToString(const std::vector<mongo::BSONElement> &ar)
        {
            std::string result = "[";
            for (std::vector<mongo::BSONElement>::const_iterator it = ar.begin();it != ar.end(); ++it)
            {
                result += bsonelement_cast<std::string>(*it);
                result +=",";
            }
            result+="]";
            return result;
        }

        std::vector<mongo::BSONElement> stringToBsonArray(const std::string &str)
        {
            std::vector<mongo::BSONElement> res;
            const char delimiters = ',';
            if(str.size() && str[0]=='[' && str[str.length()-1]==']'){
                std::string::size_type lastPos = str.find_first_not_of(delimiters, 0);
                std::string::size_type pos     = str.find_first_of(delimiters, lastPos);
                while (std::string::npos != pos || std::string::npos != lastPos)
                {
                    // Found a token, add it to the vector.
                    std::string val = str.substr(lastPos, pos - lastPos);
                    res.push_back(mongo::BSONElement(val.c_str()));
                    // Skip delimiters.  Note the "not_of"
                    lastPos = str.find_first_not_of(delimiters, pos);
                    // Find next "non-delimiter"
                    pos = str.find_first_of(delimiters, lastPos);
                }
            }
            return res;
        }

        void buildJsonString(const mongo::BSONObj &obj, std::string &con, UUIDEncoding uuid, SupportedTimes tz)
        {
            mongo::BSONObjIterator iterator(obj);
            con.append("{ \n");
            while (iterator.more())
            {
                mongo::BSONElement e = iterator.next();
                con.append("\"");
                con.append(e.fieldName());
                con.append("\"");
                con.append(" : ");
                buildJsonString(e,con,uuid,tz);
                con.append(", \n");
            }
            con.append("\n}\n\n");
        }

        void buildJsonString(const mongo::BSONElement &elem,std::string &con, UUIDEncoding uuid, SupportedTimes tz)
        {
            switch (elem.type())
            {
            case NumberDouble:
                {
                    char dob[32] = {0};
                    sprintf(dob, "%f", elem.Double());
                    con.append(dob);
                }
                break;
            case String:
                {
                    con.append(elem.valuestr(), elem.valuestrsize() - 1);
                }
                break;
            case Object:
                {
                    buildJsonString(elem.Obj(), con, uuid, tz);
                }
                break;
            case Array:
                {
                    buildJsonString(elem.Obj(), con, uuid, tz);
                }
                break;
            case BinData:
                {
                    mongo::BinDataType binType = elem.binDataType();
                    if (binType == mongo::newUUID || binType == mongo::bdtUUID) {
                        std::string uu = HexUtils::formatUuid(elem, uuid);
                        con.append(uu);
                        break;
                    }
                    con.append("<binary>");
                }
                break;
            case Undefined:
                con.append("<undefined>");
                break;
            case jstOID:
                {
                    std::string idValue = elem.OID().toString();
                    char buff[256] = {0};
                    sprintf(buff, "ObjectId(\"%s\")", idValue.c_str());
                    con.append(buff);
                }
                break;
            case Bool:
                con.append(elem.Bool() ? "true" : "false");
                break;
            case Date:
                {
                    long long ms = (long long) elem.Date().millis;

                    boost::posix_time::ptime epoch(boost::gregorian::date(1970,1,1));
                    boost::posix_time::time_duration diff = boost::posix_time::millisec(ms);
                    boost::posix_time::ptime time = epoch + diff;

                    std::string date = miutil::isotimeString(time,false,tz==LocalTime);

                    con.append(date);
                    break;
                }
            case jstNULL:
                con.append("<null>");
                break;

            case RegEx:
                {
                    con.append("/" + std::string(elem.regex()) + "/");

                    for ( const char *f = elem.regexFlags(); *f; ++f ) {
                        switch ( *f ) {
                        case 'g':
                        case 'i':
                        case 'm':
                            con+=*f;
                        default:
                            break;
                        }
                    }
                }
                break;
            case DBRef:
                break;
            case Code:
                con.append(elem._asCode());
                break;
            case Symbol:
                con.append(elem.valuestr(), elem.valuestrsize() - 1);
                break;
            case CodeWScope:
                {
                    mongo::BSONObj scope = elem.codeWScopeObject();
                    if (!scope.isEmpty() ) {
                        con.append(elem._asCode());
                        break;
                    }
                }
                break;
            case NumberInt:
                {
                    char num[16]={0};
                    sprintf(num,"%d",elem.Int());
                    con.append(num);
                    break;
                }           
            case Timestamp:
                {
                    Date_t date = elem.timestampTime();
                    unsigned long long millis = date.millis;
                    if ((long long)millis >= 0 &&
                        ((long long)millis/1000) < (std::numeric_limits<time_t>::max)()) {
                            con.append(date.toString());
                    }
                    break;
                }
            case NumberLong:
                {
                    char num[32]={0};
                    sprintf(num,"%lld",elem.Long());
                    con.append(num);
                    break; 
                }
            default:
                con.append("<unsupported>");
                break;
            }
        }
    }
}
