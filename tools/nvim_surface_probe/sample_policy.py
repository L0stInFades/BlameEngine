def score_order(order, route):
    score = route["travel_time"]
    score += route["camera_risk"] * 3
    score -= route["crowd_cover"]
    return score


if __name__ == "__main__":
    sample_order = {"id": "order.demo"}
    sample_route = {
        "travel_time": 18,
        "camera_risk": 4,
        "crowd_cover": 2,
    }
    print(score_order(sample_order, sample_route))
