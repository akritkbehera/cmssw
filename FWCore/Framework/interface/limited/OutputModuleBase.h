#ifndef FWCore_Framework_limited_OutputModuleBase_h
#define FWCore_Framework_limited_OutputModuleBase_h
// -*- C++ -*-
//
// Package:     FWCore/Framework
// Class  :     OutputModuleBase
//
/**\class OutputModuleBase OutputModuleBase.h "FWCore/Framework/interface/limited/OutputModuleBase.h"

 Description: Base class for all 'limited' OutputModules

 Usage:
    <usage>

*/
//
//

// system include files

// user include files
#include "FWCore/Framework/interface/OutputModuleCore.h"
#include "FWCore/Concurrency/interface/LimitedTaskQueue.h"

// forward declarations
namespace edm {

  namespace limited {

    class OutputModuleBase : public core::OutputModuleCore {
    public:
      template <typename U>
      friend class edm::maker::ModuleHolderT;
      template <typename T>
      friend class ::edm::WorkerT;
      template <typename T>
      friend class ::edm::OutputModuleCommunicatorT;
      typedef OutputModuleBase ModuleType;

      explicit OutputModuleBase(ParameterSet const& pset);

      OutputModuleBase(OutputModuleBase const&) = delete;             // Disallow copying and moving
      OutputModuleBase& operator=(OutputModuleBase const&) = delete;  // Disallow copying and moving

      static void fillDescription(
          ParameterSetDescription& desc,
          std::vector<std::string> const& iDefaultOutputCommands = ProductSelectorRules::defaultSelectionStrings());

      //Output modules always need writeRun and writeLumi to be called
      bool wantsGlobalRuns() const noexcept { return true; }
      bool wantsGlobalLuminosityBlocks() const noexcept { return true; }
      virtual bool wantsProcessBlocks() const noexcept = 0;
      virtual bool wantsInputProcessBlocks() const noexcept = 0;
      virtual bool wantsStreamRuns() const noexcept = 0;
      virtual bool wantsStreamLuminosityBlocks() const noexcept = 0;

      unsigned int concurrencyLimit() const { return queue_.concurrencyLimit(); }

      LimitedTaskQueue& queue() { return queue_; }

    protected:
      void doPreallocate(PreallocationConfiguration const&);

      void doBeginJob();

      void doBeginStream(StreamID id) { doBeginStream_(id); }
      void doEndStream(StreamID id) { doEndStream_(id); }

      bool doEvent(EventTransitionInfo const&, ActivityRegistry*, ModuleCallingContext const*);
      //For now this is a placeholder
      /*virtual*/ void preActionBeforeRunEventAsync(WaitingTaskHolder,
                                                    ModuleCallingContext const&,
                                                    Principal const&) const noexcept {}

    private:
      LimitedTaskQueue queue_;

      virtual void preallocStreams(unsigned int) {}
      virtual void preallocate(PreallocationConfiguration const&) {}
      virtual void doBeginStream_(StreamID) {}
      virtual void doEndStream_(StreamID) {}
      virtual void doStreamBeginRun_(StreamID, RunForOutput const&, EventSetup const&) {}
      virtual void doStreamEndRun_(StreamID, RunForOutput const&, EventSetup const&) {}
      virtual void doStreamEndRunSummary_(StreamID, RunForOutput const&, EventSetup const&) {}
      virtual void doStreamBeginLuminosityBlock_(StreamID, LuminosityBlockForOutput const&, EventSetup const&) {}
      virtual void doStreamEndLuminosityBlock_(StreamID, LuminosityBlockForOutput const&, EventSetup const&) {}
      virtual void doStreamEndLuminosityBlockSummary_(StreamID, LuminosityBlockForOutput const&, EventSetup const&) {}

      virtual void doBeginRunSummary_(RunForOutput const&, EventSetup const&) {}
      virtual void doEndRunSummary_(RunForOutput const&, EventSetup const&) {}
      virtual void doBeginLuminosityBlockSummary_(LuminosityBlockForOutput const&, EventSetup const&) {}
      virtual void doEndLuminosityBlockSummary_(LuminosityBlockForOutput const&, EventSetup const&) {}
      bool hasAcquire() const noexcept { return false; }
    };
  }  // namespace limited
}  // namespace edm
#endif
