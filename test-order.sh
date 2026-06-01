#!/bin/bash
# Test order creation on paper account
# This bypasses the strategy and directly tests the order execution pipeline

export PROPR_API_KEY=pk_live_lTTjhW9SxHnsMKnB8ouUcFTje7Z7w6M61GPI3QfpQRdfFfBu

echo "=== Fetching current BTC price from Hyperliquid ==="
BTC_PRICE=$(curl -sS 'https://api.hyperliquid.xyz/info' \
  -H 'Content-Type: application/json' \
  --data-raw '{"type":"allMids"}' | python3 -c "import sys, json; print(json.load(sys.stdin)['BTC'])")

echo "Current BTC price: \$${BTC_PRICE}"

# Calculate a small order: 0.0001 BTC (~$10 notional at $100k BTC)
QUANTITY_NANO=100000  # 0.0001 BTC in nano units (1e-9)
ENTRY_PRICE=$(python3 -c "print(int(float('${BTC_PRICE}') * 1000000))")  # Convert to micro
STOP_PRICE=$(python3 -c "print(int(float('${BTC_PRICE}') * 0.98 * 1000000))")  # 2% below

echo "Order params:"
echo "  Quantity: 0.0001 BTC"
echo "  Entry price: \$${BTC_PRICE} (${ENTRY_PRICE} micro)"
echo "  Stop loss: 2% below entry (${STOP_PRICE} micro)"

echo ""
echo "=== Creating test order via Propr API ==="

ACCOUNT_ID=$(curl -sS -H "X-API-Key: $PROPR_API_KEY" \
  "https://api.propr.xyz/v1/challenge-attempts?status=active&limit=1" | \
  python3 -c "import sys, json; print(json.load(sys.stdin)['data'][0]['accountId'])")

echo "Account ID: ${ACCOUNT_ID}"

# Generate unique intent IDs
INTENT_ENTRY="test_entry_$(date +%s)"
INTENT_STOP="test_stop_$(date +%s)"
ORDER_GROUP="test_group_$(date +%s)"

ORDER_PAYLOAD=$(cat <<EOF
{
  "orderGroupId": "${ORDER_GROUP}",
  "orders": [
    {
      "intentId": "${INTENT_ENTRY}",
      "exchange": "hyperliquid",
      "productType": "perp",
      "type": "limit",
      "timeInForce": "IOC",
      "side": "buy",
      "positionSide": "long",
      "asset": "BTC",
      "base": "BTC",
      "quote": "USDC",
      "quantity": ${QUANTITY_NANO},
      "price": ${ENTRY_PRICE},
      "reduceOnly": false
    },
    {
      "intentId": "${INTENT_STOP}",
      "exchange": "hyperliquid",
      "productType": "perp",
      "type": "stop_market",
      "side": "sell",
      "positionSide": "long",
      "asset": "BTC",
      "base": "BTC",
      "quote": "USDC",
      "quantity": ${QUANTITY_NANO},
      "triggerPrice": ${STOP_PRICE},
      "reduceOnly": true,
      "closePosition": true
    }
  ]
}
EOF
)

echo ""
echo "Sending order..."
RESPONSE=$(curl -sS -X POST \
  -H "X-API-Key: $PROPR_API_KEY" \
  -H "Content-Type: application/json" \
  "https://api.propr.xyz/v1/accounts/${ACCOUNT_ID}/orders" \
  --data "${ORDER_PAYLOAD}")

echo ""
echo "=== Response ==="
echo "$RESPONSE" | python3 -m json.tool 2>/dev/null || echo "$RESPONSE"

echo ""
echo "=== Checking order status ==="
sleep 2
curl -sS -H "X-API-Key: $PROPR_API_KEY" \
  "https://api.propr.xyz/v1/accounts/${ACCOUNT_ID}/orders?status=open" | \
  python3 -m json.tool 2>/dev/null | head -50

echo ""
echo "=== Checking positions ==="
curl -sS -H "X-API-Key: $PROPR_API_KEY" \
  "https://api.propr.xyz/v1/accounts/${ACCOUNT_ID}/positions" | \
  python3 -m json.tool 2>/dev/null | head -50
