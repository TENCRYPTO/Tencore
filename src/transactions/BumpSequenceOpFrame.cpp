// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/BumpSequenceOpFrame.h"
#include "transactions/TransactionFrame.h"
#include "crypto/SignerKey.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{
using xdr::operator==;


BumpSequenceOpFrame::BumpSequenceOpFrame(Operation const& op, OperationResult& res,
                                     TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mBumpSequence(mOperation.body.bumpSequenceOp())
{
}

ThresholdLevel
BumpSequenceOpFrame::getThresholdLevel() const
{
    // bumping sequence is low threshold
    return ThresholdLevel::LOW;
}

bool
BumpSequenceOpFrame::doApply(Application& app, LedgerDelta& delta,
                           LedgerManager& ledgerManager)
{
    // sourceAccount guaranteed to exist as precondition to calling doApply.
    AccountFrame& bumpAccount = getSourceAccount();

    SequenceNumber current = bumpAccount.getSeqNum();
    // fail if the current sequence is not in the allowed range (inclusive)
    if (mBumpSequence.range && (current < mBumpSequence.range->min || current > mBumpSequence.range->max)) {
        app.getMetrics()
            .NewMeter({"op-bump-sequence", "failure", "out-of-range"},
                    "operation")
            .Mark();
        innerResult().code(BUMP_SEQ_OUT_OF_RANGE);
        return false;
    }

    // Apply the bump (bump succeeds silently if bumpTo < current)
    bumpAccount.setSeqNum(std::max(mBumpSequence.bumpTo, current));
    bumpAccount.storeChange(delta, ledgerManager.getDatabase());

    // Return successful results
    innerResult().code(BUMP_SEQ_SUCCESS);
    app.getMetrics()
        .NewMeter({"op-bump-sequence", "success", "apply"}, "operation")
        .Mark();
    return true;
}

bool
BumpSequenceOpFrame::doCheckValid(Application& app)
{
    if (app.getLedgerManager().getCurrentLedgerVersion() < 9)
    {
        app.getMetrics()
            .NewMeter({"op-bump-sequence", "failure", "not-supported-yet"},
                    "operation")
            .Mark();
        innerResult().code(BUMP_SEQ_NOT_SUPPORTED_YET);
        return false;
    }

    // Check that we aren't self-bumping
    if (mParentTx.getEnvelope().tx.sourceAccount == getSourceID()) {
        app.getMetrics()
            .NewMeter({"op-bump-sequence", "failure", "no-self-bump"},
                    "operation")
            .Mark();
        innerResult().code(BUMP_SEQ_NO_SELF_BUMP);
        return false;
    }

    // Sanity check the range argument
    if (mBumpSequence.range) {
        if (mBumpSequence.range->max < mBumpSequence.range->min) {
            app.getMetrics()
                .NewMeter({"op-bump-sequence", "failure", "invalid-range"},
                        "operation")
                .Mark();
            innerResult().code(BUMP_SEQ_INVALID_RANGE);
            return false;
        }

    }


    return true;
}
}
