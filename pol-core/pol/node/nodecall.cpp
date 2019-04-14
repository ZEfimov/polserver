/** @file
 *
 * @par History
 */

#include "nodecall.h"
#include "../../bscript/impstr.h"
#include "../../clib/logfacility.h"
#include "../../clib/stlutil.h"
#include "../polclock.h"
#include "../uoexec.h"
#include "jsprog.h"
#include "module/objwrap.h"
#include "napi-wrap.h"
#include "nodethread.h"
#include <future>

using namespace Napi;

namespace Pol
{
namespace Node
{
unsigned long requestNumber = 0;

std::atomic_uint nextRequestId( 0 );


std::map<Core::UOExecutor*, Napi::ObjectReference> execToModuleMap;

void emitExecutorShutdowns()
{
  auto call = Node::makeCall<bool>( [&]( Napi::Env env, NodeRequest<bool>* request ) {
    for ( auto& iter : execToModuleMap )
    {
      NODELOG.Format( "[{:04x}] [exec] finalizing executor {}\n" )
          << request->reqId() << iter.first->scriptname();
      bool retVal = iter.second.Get( "emit" )
                        .As<Function>()
                        .Call( iter.second.Value(), {Napi::String::New( env, "shutdown" )} )
                        .ToBoolean()
                        .Value();
      NODELOG.Format( "[{:04x}] [exec] module.emit('shutdown') returned {}\n" )
          << request->reqId() << retVal;
      iter.second.Unref();
    }
    return true;
  } );
  call.getRef();
  execToModuleMap.clear();
}


Bscript::BObjectImp* runExecutor( Core::UOExecutor* ex )
{
  passert( ex->programType() == Bscript::Program::ProgramType::JAVASCRIPT );
  // Tell the script scheduler we will manage this executor ourselves
  if ( ex->running_to_completion() )
  {
    NODELOG << "Running executor <critical>:" << ex->scriptname() << " to external holdlist\n";
  }
  else
  {
    NODELOG << "Adding executor " << ex->pid() << ":" << ex->scriptname()
            << " to external holdlist\n";
    Core::scriptScheduler.add_externalscript( ex );
  }

  Node::JavascriptProgram* prog = static_cast<Node::JavascriptProgram*>( ex->prog_.get() );

  auto call = Node::makeCall<Bscript::BObjectImp*>(
      [prog, ex]( Napi::Env env, NodeRequest<Bscript::BObjectImp*>* request ) {
        auto obj = prog->obj.Value();
        auto reqId = request->reqId();
        NODELOG.Format( "[{:04x}] [exec] call {} , argc {}\n" )
            << request->reqId() << obj.Get( "_refId" ).As<String>().Utf8Value()
            << ex->ValueStack.size();

        Napi::Array argv = Array::New( env, ex->ValueStack.size() );
        for ( size_t i = 0; !ex->ValueStack.empty(); ++i )
        {
          Bscript::BObjectRef rightref = ex->ValueStack.back();
          ex->ValueStack.pop_back();

          Napi::Value convertedVal = Node::NodeObjectWrap::Wrap( env, rightref, reqId );

          argv[i] = convertedVal;

          NODELOG.Format( "[{:04x}] [exec] argv[{}] = {}\n" )
              << reqId << i << Node::ToUtf8Value( convertedVal );
        }

        try
        {
          auto jsCall =
              requireRef.Get( "scriptloader" )
                  .As<Object>()
                  .Get( "runScript" )
                  .As<Function>()
                  .Call( {External<weak_ptr<Core::UOExecutor>>::New(
                              env, new weak_ptr<Core::UOExecutor>( ex->weakptr ),
                              [=]( Napi::Env, weak_ptr<Core::UOExecutor>* data ) {
                                {
                                  if ( data->exists() )
                                  {
                                    NODELOG.Format( "[{:04x}] [exec] finalized {}\n" )
                                        << reqId << ex->scriptname();
                                    Core::scriptScheduler.free_externalscript(
                                        data->get_weakptr() );
                                  }
                                }
                                delete data;
                              } ),
                          Napi::String::New( env, prog->scriptname() ), prog->obj.Value(), argv} )
                  .As<Object>();

          auto jsRetVal = jsCall.Get( "value" );
          NODELOG.Format( "[{:04x}] [exec] returned value {}\n" )
              << request->reqId() << Node::ToUtf8Value( jsRetVal );
          if ( !ex->running_to_completion() )
          {
            auto mod = jsCall.Get( "module" ).As<Object>();
            if ( mod.Get( "_eventsCount" ).As<Number>().Int32Value() > 0 )
              execToModuleMap.emplace( ex, ObjectReference::New( mod, 1 ) );
          }
          // execToModuleMap.find( ex );

          return NodeObjectWrap::Wrap( env, jsRetVal, reqId )->impptr()->copy();
        }
        catch ( std::exception& ex )
        {
          POLLOG_ERROR.Format( "Error running node script {}: {}\n" )
              << prog->scriptname() << ex.what();
          return static_cast<Bscript::BObjectImp*>( Bscript::UninitObject::create() );
        }
      },
      ex->running_to_completion() );

  if ( ex->running_to_completion() )
  {
    auto* impptr = call.getRef();
    NODELOG.Format( "[{:04x}] [exec] returned to core {}\n" )
        << call.reqId() << impptr->getStringRep();
    return impptr;
  }
  return nullptr;
  // Bscript::UninitObject::create();
}


}  // namespace Node
}  // namespace Pol
